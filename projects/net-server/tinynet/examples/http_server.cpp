// HTTP 服务器：http_server [端口] [IO 线程数] [空闲超时秒数]
//   GET /        返回一行文字
//   GET /stats   返回当前的连接数
#include <cstdlib>
#include <string>

#include "tinynet/EventLoop.h"
#include "tinynet/Log.h"
#include "tinynet/http/HttpServer.h"

using namespace tinynet;
using namespace tinynet::http;

int main(int argc, char** argv)
{
    auto port = static_cast<std::uint16_t>(argc > 1 ? std::atoi(argv[1]) : 8000);
    int threads = argc > 2 ? std::atoi(argv[2]) : 0;
    int idle = argc > 3 ? std::atoi(argv[3]) : 60;

    EventLoop loop;
    HttpServer server(&loop, InetAddress(port), "http");
    server.setThreadNum(threads);
    server.setIdleTimeout(std::chrono::seconds(idle));
    server.setHttpCallback([&server](const HttpRequest& req, HttpResponse* resp) {
        resp->setContentType("text/plain; charset=utf-8");
        if (req.method != "GET") {
            resp->setStatus(405, "Method Not Allowed");
        } else if (req.path == "/") {
            resp->setBody("hello, tinynet\n");
        } else if (req.path == "/stats") {
            resp->setBody("connections: " + std::to_string(server.connectionCount()) + "\n");
        } else {
            resp->setStatus(404, "Not Found");
            resp->setBody("not found: " + req.path + "\n");
        }
    });
    server.start();
    LOG_INFO("HTTP 服务器在 %s 上监听，%d 个 IO 线程，空闲 %d 秒断开", server.listenAddress().toIpPort().c_str(), threads,
             idle);
    loop.loop();
}
