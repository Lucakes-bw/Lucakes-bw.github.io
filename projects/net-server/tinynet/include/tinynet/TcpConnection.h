#pragma once

#include <any>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "tinynet/Buffer.h"
#include "tinynet/Callbacks.h"
#include "tinynet/InetAddress.h"

namespace tinynet {

class Channel;
class EventLoop;

// 一个 TCP 连接。用 shared_ptr 管理：TcpServer、正在执行的回调、用户代码都可能持有它，
// 最后一个持有者放手时才析构、关闭 fd。
// 除了 send、shutdown、forceClose 可以在任何线程调用以外，其他操作都在它所属的 IO 线程中进行。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop* loop, std::string name, int fd, const InetAddress& local, const InetAddress& peer);
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    EventLoop* loop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return local_; }
    const InetAddress& peerAddress() const { return peer_; }
    bool connected() const { return state_ == kConnected; }

    void send(std::string_view data);       // 线程安全
    void send(Buffer* buf);                 // 发送并清空 buf，线程安全
    void shutdown();                        // 输出缓冲区发完以后关闭写端（半关闭）
    void forceClose();                      // 立即关闭
    void setTcpNoDelay(bool on);

    // 用户数据，比如 HTTP 解析的状态
    void setContext(std::any context) { context_ = std::move(context); }
    std::any* context() { return &context_; }

    Buffer* inputBuffer() { return &inputBuffer_; }
    Buffer* outputBuffer() { return &outputBuffer_; }

    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }
    void setHighWaterMarkCallback(HighWaterMarkCallback cb, std::size_t mark)
    {
        highWaterMarkCallback_ = std::move(cb);
        highWaterMark_ = mark;
    }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }   // 给 TcpServer 用

    void connectEstablished();              // TcpServer 在 IO 线程中调用，只调用一次
    void connectDestroyed();                // TcpServer 删除这个连接时调用，只调用一次

private:
    enum State { kConnecting, kConnected, kDisconnecting, kDisconnected };

    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(std::string_view data);
    void shutdownInLoop();
    void forceCloseInLoop();

    EventLoop* loop_;
    const std::string name_;
    std::atomic<State> state_{kConnecting}; // send、shutdown 可能在别的线程中读取
    const int fd_;
    std::unique_ptr<Channel> channel_;
    const InetAddress local_;
    const InetAddress peer_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;
    std::size_t highWaterMark_ = 64 * 1024 * 1024;
    Buffer inputBuffer_;
    Buffer outputBuffer_;
    std::any context_;
};

}  // namespace tinynet
