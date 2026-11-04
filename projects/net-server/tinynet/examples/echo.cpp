// 回声服务器：echo [端口] [IO 线程数]
#include <cstdlib>

#include "tinynet/Buffer.h"
#include "tinynet/EventLoop.h"
#include "tinynet/Log.h"
#include "tinynet/TcpConnection.h"
#include "tinynet/TcpServer.h"

using namespace tinynet;

int main(int argc, char** argv)
{
    auto port = static_cast<std::uint16_t>(argc > 1 ? std::atoi(argv[1]) : 9000);
    int threads = argc > 2 ? std::atoi(argv[2]) : 0;

    EventLoop loop;
    TcpServer server(&loop, InetAddress(port), "echo");
    server.setThreadNum(threads);
    server.setConnectionCallback([](const TcpConnectionPtr& conn) {
        LOG_INFO("%s %s", conn->peerAddress().toIpPort().c_str(), conn->connected() ? "连接" : "断开");
    });
    server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) { conn->send(buf); });
    server.start();
    LOG_INFO("回声服务器在 %s 上监听，%d 个 IO 线程", server.listenAddress().toIpPort().c_str(), threads);
    loop.loop();
}
