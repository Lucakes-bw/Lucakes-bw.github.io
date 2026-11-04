#pragma once

#include <chrono>
#include <functional>

#include "tinynet/TcpServer.h"
#include "tinynet/http/HttpRequest.h"
#include "tinynet/http/HttpResponse.h"

namespace tinynet::http {

// 在 TcpServer 上加一层：解析请求，调用用户的处理函数，发送响应，处理持久连接和空闲超时
class HttpServer {
public:
    using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

    HttpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name, bool reusePort = false);

    void setHttpCallback(HttpCallback cb) { httpCallback_ = std::move(cb); }
    void setThreadNum(int n) { server_.setThreadNum(n); }
    // 连接空闲超过这么久就关闭，0 表示不限制。start 之前调用
    void setIdleTimeout(std::chrono::seconds t) { idleTimeout_ = t; }
    void start() { server_.start(); }
    InetAddress listenAddress() const { return server_.listenAddress(); }
    std::size_t connectionCount() const { return server_.connectionCount(); }

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf);
    void checkIdle(const std::weak_ptr<TcpConnection>& weak);

    TcpServer server_;
    HttpCallback httpCallback_;
    std::chrono::seconds idleTimeout_{0};
};

}  // namespace tinynet::http
