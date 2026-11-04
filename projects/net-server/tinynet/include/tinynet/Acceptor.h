#pragma once

#include <functional>

#include "tinynet/Channel.h"
#include "tinynet/InetAddress.h"

namespace tinynet {

class EventLoop;

// 监听套接字：有新连接时 accept，把新连接的 fd 交给回调
class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int fd, const InetAddress& peer)>;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reusePort);
    ~Acceptor();
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void setNewConnectionCallback(NewConnectionCallback cb) { newConnectionCallback_ = std::move(cb); }
    void listen();
    InetAddress listenAddress() const;      // 实际监听的地址：端口写 0 时由系统分配

private:
    void handleRead();

    EventLoop* loop_;
    int acceptFd_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    int idleFd_;                            // 预留的一个 fd，文件描述符用完时用来拒绝连接
};

}  // namespace tinynet
