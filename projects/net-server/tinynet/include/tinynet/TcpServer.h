#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include "tinynet/Callbacks.h"
#include "tinynet/InetAddress.h"

namespace tinynet {

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

// TCP 服务器：Acceptor 在 baseLoop 中接受连接，新连接交给某个 IO 线程的 EventLoop
class TcpServer {
public:
    TcpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name, bool reusePort = false);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void setThreadNum(int n);               // IO 线程的个数，0 表示所有连接都在 baseLoop 中处理。start 之前调用
    void start();                           // 开始监听，可以调用多次
    InetAddress listenAddress() const;

    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }

    std::size_t connectionCount() const { return connectionCount_; }   // 任何线程都可以读

private:
    void newConnection(int fd, const InetAddress& peer);
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

    EventLoop* loop_;                       // baseLoop，Acceptor 所在的 loop
    const std::string name_;
    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<EventLoopThreadPool> threadPool_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    std::atomic<bool> started_{false};
    int nextConnId_ = 1;
    std::map<std::string, TcpConnectionPtr> connections_;      // 只在 baseLoop 中访问
    std::atomic<std::size_t> connectionCount_{0};
    // IO 线程中的连接关闭时，要通知 baseLoop 删除它；那时 TcpServer 可能已经析构了。
    // 通知中带着这个对象的 weak_ptr，执行时先检查服务器还在不在
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tinynet
