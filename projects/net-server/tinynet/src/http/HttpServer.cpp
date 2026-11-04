#include "tinynet/http/HttpServer.h"

#include <any>

#include "tinynet/EventLoop.h"
#include "tinynet/TcpConnection.h"
#include "tinynet/http/HttpParser.h"

namespace tinynet::http {

namespace {

// 每个连接的状态，放在 TcpConnection::context 中
struct ConnState {
    HttpParser parser;
    Clock::time_point lastActive;
};

}  // namespace

HttpServer::HttpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name, bool reusePort)
    : server_(loop, listenAddr, std::move(name), reusePort)
{
    server_.setConnectionCallback([this](const TcpConnectionPtr& c) { onConnection(c); });
    server_.setMessageCallback([this](const TcpConnectionPtr& c, Buffer* b) { onMessage(c, b); });
}

void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
    if (!conn->connected()) return;
    conn->setTcpNoDelay(true);
    conn->setContext(ConnState{HttpParser(), Clock::now()});
    if (idleTimeout_.count() > 0) {
        std::weak_ptr<TcpConnection> weak(conn);   // 定时器不应该让连接一直活着
        conn->loop()->runAfter(idleTimeout_, [this, weak] { checkIdle(weak); });
    }
}

// 每个连接只有一个定时器：到期时看看最后一次活动是什么时候。
// 还没有空闲够，就按剩下的时间重新设一个；空闲够了，关闭连接。
// 这样收到数据时只需要更新 lastActive，不用每次都取消、重新添加定时器。
void HttpServer::checkIdle(const std::weak_ptr<TcpConnection>& weak)
{
    TcpConnectionPtr conn = weak.lock();
    if (!conn || !conn->connected()) return;
    auto* state = std::any_cast<ConnState>(conn->context());
    auto idle = Clock::now() - state->lastActive;
    if (idle >= idleTimeout_) {
        conn->forceClose();
    } else {
        conn->loop()->runAfter(idleTimeout_ - idle, [this, weak] { checkIdle(weak); });
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf)
{
    auto* state = std::any_cast<ConnState>(conn->context());
    state->lastActive = Clock::now();
    Buffer out;
    bool close = false;
    // 一次可能收到好几个请求（流水线），逐个处理，响应攒在一起发送
    while (!close) {
        HttpParser::Result r = state->parser.parse(buf);
        if (r == HttpParser::Result::kNeedMore) break;
        if (r == HttpParser::Result::kBadRequest) {
            HttpResponse resp(true);
            resp.setStatus(400, "Bad Request");
            resp.appendTo(&out);
            close = true;
            break;
        }
        const HttpRequest& req = state->parser.request();
        HttpResponse resp(!req.keepAlive());
        if (httpCallback_) httpCallback_(req, &resp);
        else resp.setStatus(404, "Not Found");
        resp.appendTo(&out);
        close = resp.closeConnection();
        state->parser.reset();
    }
    if (out.readableBytes() > 0) conn->send(&out);
    if (close) conn->shutdown();            // 发完响应以后关闭写端
}

}  // namespace tinynet::http
