#include "tinynet/Buffer.h"

#include <sys/uio.h>

#include <cerrno>

namespace tinynet {

// 读多少合适？事先不知道有多少数据。先读进缓冲区剩下的空间，不够的部分读进栈上一块 64 KB 的临时空间，
// 再追加到缓冲区中。这样一次 readv 系统调用就能读到足够多的数据，缓冲区又不用预先分配得很大。
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    char extrabuf[65536];
    const std::size_t writable = writableBytes();
    iovec vec[2];
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;
    const int iovcnt = writable < sizeof extrabuf ? 2 : 1;   // 缓冲区本身已经够大，就不用临时空间
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<std::size_t>(n) <= writable) {
        writerIndex_ += static_cast<std::size_t>(n);
    } else {
        writerIndex_ = buffer_.size();
        append(std::string_view(extrabuf, static_cast<std::size_t>(n) - writable));
    }
    return n;
}

}  // namespace tinynet
