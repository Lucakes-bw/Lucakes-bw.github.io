#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <sys/types.h>
#include <string>
#include <string_view>
#include <vector>

namespace tinynet {

// 应用层缓冲区
//
// +-------------------+------------------+------------------+
// | prependable bytes |  readable bytes  |  writable bytes  |
// |                   |    （数据）       |    （空闲）       |
// +-------------------+------------------+------------------+
// 0      <=      readerIndex   <=   writerIndex    <=     size
//
// 读走数据只是把 readerIndex 往后移；写入时空间不够，先把数据挪到前面，还不够再扩容。
// 前面预留的 8 个字节可以在数据前面插入长度等头部，不用移动数据。
class Buffer {
public:
    static constexpr std::size_t kCheapPrepend = 8;
    static constexpr std::size_t kInitialSize = 1024;

    explicit Buffer(std::size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize), readerIndex_(kCheapPrepend), writerIndex_(kCheapPrepend)
    {
    }

    std::size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    std::size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    std::size_t prependableBytes() const { return readerIndex_; }

    const char* peek() const { return begin() + readerIndex_; }       // 第一个可读的字节
    std::string_view view() const { return {peek(), readableBytes()}; }

    // 在可读的数据中查找 "\r\n"，返回它的位置，没有返回 nullptr
    const char* findCRLF(const char* start = nullptr) const
    {
        const char* from = start ? start : peek();
        std::string_view rest(from, static_cast<std::size_t>(beginWrite() - from));
        std::size_t pos = rest.find("\r\n");
        return pos == std::string_view::npos ? nullptr : from + pos;
    }

    void retrieve(std::size_t len)
    {
        assert(len <= readableBytes());
        if (len < readableBytes()) readerIndex_ += len;
        else retrieveAll();
    }
    void retrieveUntil(const char* end) { retrieve(static_cast<std::size_t>(end - peek())); }
    void retrieveAll() { readerIndex_ = writerIndex_ = kCheapPrepend; }   // 全部读走时两个下标都回到开头

    std::string retrieveAsString(std::size_t len)
    {
        std::string s(peek(), len);
        retrieve(len);
        return s;
    }
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); }

    void append(std::string_view data)
    {
        ensureWritable(data.size());
        std::copy(data.begin(), data.end(), beginWrite());
        writerIndex_ += data.size();
    }

    void prepend(const void* data, std::size_t len)
    {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        std::memcpy(begin() + readerIndex_, data, len);
    }

    void ensureWritable(std::size_t len)
    {
        if (writableBytes() < len) makeSpace(len);
    }
    char* beginWrite() { return begin() + writerIndex_; }
    const char* beginWrite() const { return begin() + writerIndex_; }
    void hasWritten(std::size_t len) { writerIndex_ += len; }

    std::size_t capacity() const { return buffer_.size(); }

    // 从 fd 读取数据。返回 read 的结果，出错时 savedErrno 是 errno
    ssize_t readFd(int fd, int* savedErrno);

private:
    char* begin() { return buffer_.data(); }
    const char* begin() const { return buffer_.data(); }

    void makeSpace(std::size_t len)
    {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
            buffer_.resize(writerIndex_ + len);         // 前后的空闲加起来也不够：扩容
        } else {
            std::size_t readable = readableBytes();     // 够：把数据挪到最前面，腾出后面的空间
            std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }

    std::vector<char> buffer_;
    std::size_t readerIndex_;
    std::size_t writerIndex_;
};

}  // namespace tinynet
