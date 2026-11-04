#include "tinynet/http/HttpServer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "test_util.h"

using namespace std::chrono_literals;
using namespace tinynet;
using namespace tinynet::http;
using testutil::Client;

namespace {

std::unique_ptr<HttpServer> makeHttp(EventLoop* loop, std::chrono::seconds idle)
{
    auto server = std::make_unique<HttpServer>(loop, InetAddress(0, true), "http");
    server->setIdleTimeout(idle);
    server->setHttpCallback([](const HttpRequest& req, HttpResponse* resp) { resp->setBody("path=" + req.path); });
    server->start();
    return server;
}

}  // namespace

// 同一个连接上依次发两个请求（持久连接）
TEST(HttpServer, KeepAlive)
{
    testutil::ServerThread<HttpServer> st([](EventLoop* loop) { return makeHttp(loop, 0s); });
    Client c(st.server()->listenAddress().port());
    std::string expect1 = "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 7\r\n\r\npath=/a";
    ASSERT_TRUE(c.sendAll("GET /a HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_EQ(c.recvExactly(expect1.size()), expect1);
    ASSERT_TRUE(c.sendAll("GET /bb HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_EQ(c.recvExactly(expect1.size() + 1), "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 8\r\n\r\npath=/bb");
}

// 一次发两个请求，第二个要求关闭连接：两个响应都要收到，然后连接关闭
TEST(HttpServer, PipelinedThenClose)
{
    testutil::ServerThread<HttpServer> st([](EventLoop* loop) { return makeHttp(loop, 0s); });
    Client c(st.server()->listenAddress().port());
    ASSERT_TRUE(c.sendAll("GET /1 HTTP/1.1\r\n\r\nGET /2 HTTP/1.1\r\nConnection: close\r\n\r\n"));
    std::string all = c.recvUntilClose();
    EXPECT_NE(all.find("path=/1"), std::string::npos);
    EXPECT_NE(all.find("Connection: close\r\nContent-Length: 7\r\n\r\npath=/2"), std::string::npos);
}

TEST(HttpServer, BadRequestGets400)
{
    testutil::ServerThread<HttpServer> st([](EventLoop* loop) { return makeHttp(loop, 0s); });
    Client c(st.server()->listenAddress().port());
    ASSERT_TRUE(c.sendAll("HELLO\r\n\r\n"));
    EXPECT_EQ(c.recvUntilClose(), "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}

// 空闲超时：1 秒没有数据就关闭；中间有请求时重新计时
TEST(HttpServer, IdleTimeout)
{
    testutil::ServerThread<HttpServer> st([](EventLoop* loop) { return makeHttp(loop, 1s); });
    Client c(st.server()->listenAddress().port());
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(600ms);
    ASSERT_TRUE(c.sendAll("GET / HTTP/1.1\r\n\r\n"));     // 0.6 秒时有一个请求，空闲时间重新算
    std::string all = c.recvUntilClose();               // 读到连接被关闭为止
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_NE(all.find("path=/"), std::string::npos);
    EXPECT_GE(elapsed, 1500ms);                         // 0.6 + 1 秒左右才关闭
    EXPECT_LT(elapsed, 3s);
}
