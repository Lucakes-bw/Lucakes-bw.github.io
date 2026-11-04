#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "test_util.h"
#include "tinynet/Buffer.h"
#include "tinynet/TcpConnection.h"
#include "tinynet/TcpServer.h"

using namespace tinynet;
using testutil::Client;

namespace {

// 回声服务器，IO 线程数由参数决定
std::unique_ptr<TcpServer> makeEcho(EventLoop* loop, int threads)
{
    auto server = std::make_unique<TcpServer>(loop, InetAddress(0, true), "echo");   // 端口 0：由系统分配
    server->setThreadNum(threads);
    server->setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) { conn->send(buf); });
    server->start();
    return server;
}

}  // namespace

class EchoTest : public ::testing::TestWithParam<int> {};

TEST_P(EchoTest, EchoesBack)
{
    testutil::ServerThread<TcpServer> st([](EventLoop* loop) { return makeEcho(loop, GetParam()); });
    std::uint16_t port = st.server()->listenAddress().port();
    Client c(port);
    ASSERT_TRUE(c.connected());
    ASSERT_TRUE(c.sendAll("hello"));
    EXPECT_EQ(c.recvExactly(5), "hello");
}

// 一次发送 1 MB：服务器一次写不完，要放进输出缓冲区，等可写事件
TEST_P(EchoTest, LargeMessage)
{
    testutil::ServerThread<TcpServer> st([](EventLoop* loop) { return makeEcho(loop, GetParam()); });
    Client c(st.server()->listenAddress().port());
    std::string big(1 << 20, 'x');
    for (std::size_t i = 0; i < big.size(); i += 4096) big[i] = static_cast<char>('a' + i / 4096 % 26);
    std::thread writer([&] { c.sendAll(big); });
    std::string got = c.recvExactly(big.size());
    writer.join();
    EXPECT_EQ(got.size(), big.size());
    EXPECT_TRUE(got == big);
}

TEST_P(EchoTest, ManyClientsAndConnectionCount)
{
    testutil::ServerThread<TcpServer> st([](EventLoop* loop) { return makeEcho(loop, GetParam()); });
    std::uint16_t port = st.server()->listenAddress().port();
    {
        std::vector<std::unique_ptr<Client>> clients;
        for (int i = 0; i < 20; ++i) clients.push_back(std::make_unique<Client>(port));
        for (int i = 0; i < 20; ++i) {
            std::string msg = "msg" + std::to_string(i);
            ASSERT_TRUE(clients[static_cast<std::size_t>(i)]->sendAll(msg));
            EXPECT_EQ(clients[static_cast<std::size_t>(i)]->recvExactly(msg.size()), msg);
        }
        EXPECT_TRUE(testutil::waitFor([&] { return st.server()->connectionCount() == 20; }));
    }
    // 客户端全部关闭以后，服务器上的连接也要全部删除
    EXPECT_TRUE(testutil::waitFor([&] { return st.server()->connectionCount() == 0; }));
}

INSTANTIATE_TEST_SUITE_P(IoThreads, EchoTest, ::testing::Values(0, 1, 3));

// 半关闭：客户端关闭写端以后，服务器还能把最后的数据发回来
TEST(TcpServer, HalfClose)
{
    testutil::ServerThread<TcpServer> st([](EventLoop* loop) {
        auto server = std::make_unique<TcpServer>(loop, InetAddress(0, true), "half");
        server->setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) {
            conn->send(buf->retrieveAllAsString() + "!");
            conn->shutdown();               // 发完以后关闭写端
        });
        server->start();
        return server;
    });
    Client c(st.server()->listenAddress().port());
    c.sendAll("bye");
    c.shutdownWrite();
    EXPECT_EQ(c.recvUntilClose(), "bye!");
}

// 从别的线程（不是连接所在的 IO 线程）调用 send
TEST(TcpServer, SendFromAnotherThread)
{
    std::promise<TcpConnectionPtr> got;
    auto gotFuture = got.get_future();
    testutil::ServerThread<TcpServer> st([&got](EventLoop* loop) {
        auto server = std::make_unique<TcpServer>(loop, InetAddress(0, true), "push");
        server->setThreadNum(2);
        server->setConnectionCallback([&got](const TcpConnectionPtr& conn) {
            if (conn->connected()) got.set_value(conn);
        });
        server->start();
        return server;
    });
    Client c(st.server()->listenAddress().port());
    TcpConnectionPtr conn = gotFuture.get();
    std::thread pusher([conn] {
        for (int i = 0; i < 100; ++i) conn->send("0123456789");
    });
    pusher.join();
    EXPECT_EQ(c.recvExactly(1000).size(), 1000u);
}
