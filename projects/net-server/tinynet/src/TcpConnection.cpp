#include "tinynet/TcpConnection.h"

#include <unistd.h>

#include <cassert>
#include <cerrno>

#include "tinynet/Channel.h"
#include "tinynet/EventLoop.h"
#include "tinynet/Log.h"
#include "tinynet/Sockets.h"

namespace tinynet {

TcpConnection::TcpConnection(EventLoop* loop, std::string name, int fd, const InetAddress& local,
                             const InetAddress& peer)
    : loop_(loop), name_(std::move(name)), fd_(fd), channel_(std::make_unique<Channel>(loop, fd)), local_(local),
      peer_(peer)
{
    channel_->setReadCallback([this] { handleRead(); });
    channel_->setWriteCallback([this] { handleWrite(); });
    channel_->setCloseCallback([this] { handleClose(); });
    channel_->setErrorCallback([this] { handleError(); });
    sockets::setKeepAlive(fd_, true);
}

TcpConnection::~TcpConnection()
{
    assert(state_ == kDisconnected);
    sockets::close(fd_);                    // fd 由 TcpConnection 拥有，最后一个 shared_ptr 放手时关闭
}

void TcpConnection::connectEstablished()
{
    loop_->assertInLoopThread();
    assert(state_ == kConnecting);
    state_ = kConnected;
    channel_->tie(shared_from_this());      // 处理这个连接的事件期间，保证它不会被析构
    channel_->enableReading();
    if (connectionCallback_) connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting) {   // 没有经过 handleClose 就被删除（比如服务器析构）
        state_ = kDisconnected;
        channel_->disableAll();
        if (connectionCallback_) connectionCallback_(shared_from_this());
    }
    channel_->remove();
}

void TcpConnection::handleRead()
{
    loop_->assertInLoopThread();
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(fd_, &savedErrno);
    if (n > 0) {
        if (messageCallback_) messageCallback_(shared_from_this(), &inputBuffer_);
    } else if (n == 0) {
        handleClose();                      // 对方关闭了连接（或者关闭了写端）
    } else if (savedErrno != EAGAIN && savedErrno != EWOULDBLOCK) {
        errno = savedErrno;
        LOG_SYSERR("连接 %s 读取出错", name_.c_str());
        handleError();
        handleClose();
    }
}

void TcpConnection::handleWrite()
{
    loop_->assertInLoopThread();
    if (!channel_->isWriting()) return;     // 已经不关心可写事件了
    ssize_t n = ::write(fd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) LOG_SYSERR("连接 %s 写入出错", name_.c_str());
        return;
    }
    outputBuffer_.retrieve(static_cast<std::size_t>(n));
    if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();         // 发完了，不再关心可写事件，否则 epoll 会不停地报告
        if (writeCompleteCallback_) {
            auto self = shared_from_this();
            loop_->queueInLoop([this, self] { writeCompleteCallback_(self); });
        }
        if (state_ == kDisconnecting) shutdownInLoop();   // 之前要求过关闭，现在可以关了
    }
}

void TcpConnection::handleClose()
{
    loop_->assertInLoopThread();
    if (state_ == kDisconnected) return;
    state_ = kDisconnected;
    channel_->disableAll();
    TcpConnectionPtr guard(shared_from_this());           // 下面的回调中 TcpServer 会删掉它持有的指针
    if (connectionCallback_) connectionCallback_(guard);
    if (closeCallback_) closeCallback_(guard);
}

void TcpConnection::handleError()
{
    int err = sockets::getSocketError(fd_);
    LOG_ERROR("连接 %s 出错：%s", name_.c_str(), std::strerror(err));
}

void TcpConnection::send(std::string_view data)
{
    if (state_ != kConnected) return;
    if (loop_->isInLoopThread()) {
        sendInLoop(data);
    } else {
        // 别的线程：data 指向的内存在交给 IO 线程以后可能已经无效了，复制一份
        auto self = shared_from_this();
        loop_->runInLoop([this, self, copy = std::string(data)] { sendInLoop(copy); });
    }
}

void TcpConnection::send(Buffer* buf)
{
    send(buf->view());
    buf->retrieveAll();
}

void TcpConnection::sendInLoop(std::string_view data)
{
    loop_->assertInLoopThread();
    if (state_ == kDisconnected) return;
    std::size_t written = 0;
    // 输出缓冲区是空的：先直接写一次，大多数情况下能全部写完，就不用经过缓冲区了
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        ssize_t n = ::write(fd_, data.data(), data.size());
        if (n >= 0) {
            written = static_cast<std::size_t>(n);
            if (written == data.size() && writeCompleteCallback_) {
                auto self = shared_from_this();
                loop_->queueInLoop([this, self] { writeCompleteCallback_(self); });
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_SYSERR("连接 %s 写入出错", name_.c_str());
            if (errno == EPIPE || errno == ECONNRESET) return;
        }
    }
    // 没写完的放进输出缓冲区，等可写事件
    if (written < data.size()) {
        std::size_t old = outputBuffer_.readableBytes();
        std::size_t remaining = data.size() - written;
        if (old + remaining >= highWaterMark_ && old < highWaterMark_ && highWaterMarkCallback_) {
            auto self = shared_from_this();
            loop_->queueInLoop([this, self, n = old + remaining] { highWaterMarkCallback_(self, n); });
        }
        outputBuffer_.append(data.substr(written));
        if (!channel_->isWriting()) channel_->enableWriting();
    }
}

void TcpConnection::shutdown()
{
    if (state_ != kConnected) return;
    state_ = kDisconnecting;
    auto self = shared_from_this();
    loop_->runInLoop([this, self] { shutdownInLoop(); });
}

void TcpConnection::shutdownInLoop()
{
    loop_->assertInLoopThread();
    if (!channel_->isWriting()) sockets::shutdownWrite(fd_);   // 还有数据没发完，等 handleWrite 发完再关
}

void TcpConnection::forceClose()
{
    if (state_ == kConnected || state_ == kDisconnecting) {
        state_ = kDisconnecting;
        auto self = shared_from_this();
        loop_->queueInLoop([this, self] { forceCloseInLoop(); });
    }
}

void TcpConnection::forceCloseInLoop()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting) handleClose();
}

void TcpConnection::setTcpNoDelay(bool on) { sockets::setTcpNoDelay(fd_, on); }

}  // namespace tinynet
