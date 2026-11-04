#include "tinynet/Acceptor.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

#include "tinynet/EventLoop.h"
#include "tinynet/Log.h"
#include "tinynet/Sockets.h"

namespace tinynet {

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reusePort)
    : loop_(loop),
      acceptFd_(sockets::createNonblockingOrDie()),
      acceptChannel_(loop, acceptFd_),
      idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
{
    sockets::setReuseAddr(acceptFd_, true);
    sockets::setReusePort(acceptFd_, reusePort);
    sockets::bindOrDie(acceptFd_, listenAddr);
    acceptChannel_.setReadCallback([this] { handleRead(); });
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
    sockets::close(acceptFd_);
    ::close(idleFd_);
}

void Acceptor::listen()
{
    loop_->assertInLoopThread();
    sockets::listenOrDie(acceptFd_);
    acceptChannel_.enableReading();
}

InetAddress Acceptor::listenAddress() const { return sockets::localAddress(acceptFd_); }

void Acceptor::handleRead()
{
    loop_->assertInLoopThread();
    // 一次把已经完成握手的连接都接受下来，直到 EAGAIN
    for (;;) {
        InetAddress peer;
        int connfd = sockets::accept(acceptFd_, &peer);
        if (connfd >= 0) {
            if (newConnectionCallback_) newConnectionCallback_(connfd, peer);
            else sockets::close(connfd);
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        if (errno == EMFILE) {
            // 文件描述符用完了：连接一直留在监听队列中，epoll 会不停地报告可读，变成忙循环。
            // 先关掉预留的 fd，腾出一个位置把这个连接接受下来、立即关闭，再把预留的 fd 打开
            ::close(idleFd_);
            idleFd_ = ::accept(acceptFd_, nullptr, nullptr);
            ::close(idleFd_);
            idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            LOG_ERROR("文件描述符用完了，拒绝了一个连接");
            return;
        }
        LOG_SYSERR("accept");
        return;
    }
}

}  // namespace tinynet
