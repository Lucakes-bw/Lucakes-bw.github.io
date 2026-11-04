#include "tinynet/TcpServer.h"

#include <cassert>

#include "tinynet/Acceptor.h"
#include "tinynet/EventLoop.h"
#include "tinynet/EventLoopThreadPool.h"
#include "tinynet/Sockets.h"
#include "tinynet/TcpConnection.h"

namespace tinynet {

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name, bool reusePort)
    : loop_(loop),
      name_(std::move(name)),
      acceptor_(std::make_unique<Acceptor>(loop, listenAddr, reusePort)),
      threadPool_(std::make_unique<EventLoopThreadPool>(loop))
{
    acceptor_->setNewConnectionCallback([this](int fd, const InetAddress& peer) { newConnection(fd, peer); });
}

TcpServer::~TcpServer()
{
    loop_->assertInLoopThread();
    for (auto& [name, conn] : connections_) {
        TcpConnectionPtr c = conn;
        conn.reset();
        c->loop()->runInLoop([c] { c->connectDestroyed(); });
    }
}

void TcpServer::setThreadNum(int n) { threadPool_->setThreadNum(n); }

void TcpServer::start()
{
    if (started_.exchange(true)) return;
    threadPool_->start();
    loop_->runInLoop([this] { acceptor_->listen(); });
}

InetAddress TcpServer::listenAddress() const { return acceptor_->listenAddress(); }

void TcpServer::newConnection(int fd, const InetAddress& peer)
{
    loop_->assertInLoopThread();
    EventLoop* ioLoop = threadPool_->getNextLoop();         // 轮流分给各个 IO 线程
    std::string name = name_ + "#" + std::to_string(nextConnId_++);
    auto conn = std::make_shared<TcpConnection>(ioLoop, name, fd, sockets::localAddress(fd), peer);
    connections_[name] = conn;
    ++connectionCount_;
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    // 关闭回调在 IO 线程中执行，只能用 baseLoop 和 alive_ 的 weak_ptr，不能直接访问 TcpServer 的成员
    conn->setCloseCallback([this, base = loop_, alive = std::weak_ptr<int>(alive_)](const TcpConnectionPtr& c) {
        base->runInLoop([this, alive, c] {
            if (alive.lock()) removeConnectionInLoop(c);   // 在 baseLoop 中检查：服务器的析构也在这个线程
        });
    });
    ioLoop->runInLoop([conn] { conn->connectEstablished(); });   // 从此以后，这个连接只在 ioLoop 中处理
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    loop_->assertInLoopThread();
    connections_.erase(conn->name());
    --connectionCount_;
    // 不能在这里直接析构：可能正处在这个连接的 Channel::handleEvent 中。
    // 用 queueInLoop 推迟到本轮事件处理完以后，lambda 持有的 conn 是最后一个 shared_ptr
    conn->loop()->queueInLoop([conn] { conn->connectDestroyed(); });
}

}  // namespace tinynet
