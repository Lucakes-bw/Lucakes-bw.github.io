#include "tinynet/Buffer.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <string>
#include <thread>

using tinynet::Buffer;

TEST(Buffer, AppendAndRetrieve)
{
    Buffer buf;
    EXPECT_EQ(buf.readableBytes(), 0u);
    EXPECT_EQ(buf.writableBytes(), Buffer::kInitialSize);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend);

    buf.append(std::string(200, 'x'));
    EXPECT_EQ(buf.readableBytes(), 200u);
    EXPECT_EQ(buf.retrieveAsString(50), std::string(50, 'x'));
    EXPECT_EQ(buf.readableBytes(), 150u);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend + 50);

    buf.retrieveAll();                      // 全部读走：两个下标回到开头
    EXPECT_EQ(buf.readableBytes(), 0u);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend);
}

TEST(Buffer, GrowsWhenFull)
{
    Buffer buf;
    buf.append(std::string(400, 'a'));
    buf.append(std::string(1000, 'b'));     // 400 + 1000 > 1024：扩容
    EXPECT_EQ(buf.readableBytes(), 1400u);
    EXPECT_GE(buf.capacity(), Buffer::kCheapPrepend + 1400);
    EXPECT_EQ(buf.retrieveAllAsString(), std::string(400, 'a') + std::string(1000, 'b'));
}

TEST(Buffer, MovesDataInsteadOfGrowing)
{
    Buffer buf;
    buf.append(std::string(800, 'a'));
    buf.retrieve(700);                      // 前面空出了 700 字节
    std::size_t cap = buf.capacity();
    buf.append(std::string(500, 'b'));      // 后面只有 224 字节，但前后加起来够：挪动数据，不扩容
    EXPECT_EQ(buf.capacity(), cap);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend);
    EXPECT_EQ(buf.retrieveAllAsString(), std::string(100, 'a') + std::string(500, 'b'));
}

TEST(Buffer, FindCRLF)
{
    Buffer buf;
    buf.append("GET / HTTP/1.1\r\nHost: x\r\n");
    const char* crlf = buf.findCRLF();
    ASSERT_NE(crlf, nullptr);
    EXPECT_EQ(std::string(buf.peek(), crlf), "GET / HTTP/1.1");
    buf.retrieveUntil(crlf + 2);
    EXPECT_EQ(std::string(buf.peek(), buf.findCRLF()), "Host: x");
    buf.retrieve(buf.readableBytes());
    buf.append("no newline");
    EXPECT_EQ(buf.findCRLF(), nullptr);
}

TEST(Buffer, Prepend)
{
    Buffer buf;
    buf.append("payload");
    std::uint32_t len = 7;
    buf.prepend(&len, sizeof len);          // 在数据前面加上 4 字节的长度，不用移动数据
    EXPECT_EQ(buf.readableBytes(), 11u);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend - 4);
}

// readFd：一次读到的数据超过缓冲区剩余的空间时，多出的部分经过栈上的临时空间追加进来
TEST(Buffer, ReadFdLargerThanBuffer)
{
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    std::string data(50000, 'z');
    data[0] = 'A';
    data[49999] = 'Z';
    std::thread writer([&] {
        std::size_t off = 0;
        while (off < data.size()) off += static_cast<std::size_t>(::write(fds[1], data.data() + off, data.size() - off));
        ::close(fds[1]);
    });
    Buffer buf;
    int err = 0;
    ssize_t n;
    while ((n = buf.readFd(fds[0], &err)) > 0) {}
    writer.join();
    ::close(fds[0]);
    EXPECT_EQ(buf.retrieveAllAsString(), data);
}
