#pragma once

// 测试用的小工具：在后台线程中运行一个 EventLoop 和服务器；阻塞的客户端
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "tinynet/EventLoop.h"

namespace testutil {

// 在新线程中创建 EventLoop，调用 setup（在 loop 线程中创建服务器），然后运行 loop。析构时退出并等待
template <class Server>
class ServerThread {
public:
    using Setup = std::function<std::unique_ptr<Server>(tinynet::EventLoop*)>;

    explicit ServerThread(Setup setup)
    {
        std::promise<void> ready;
        auto readyFuture = ready.get_future();
        thread_ = std::thread([this, setup = std::move(setup), &ready] {
            tinynet::EventLoop loop;
            server_ = setup(&loop);
            loop_ = &loop;
            ready.set_value();
            loop.loop();
            server_.reset();                // 服务器在 loop 线程中析构
        });
        readyFuture.wait();
    }
    ~ServerThread()
    {
        loop_->quit();
        thread_.join();
    }
    Server* server() { return server_.get(); }
    tinynet::EventLoop* loop() { return loop_; }

private:
    std::thread thread_;
    tinynet::EventLoop* loop_ = nullptr;
    std::unique_ptr<Server> server_;
};

// 阻塞的 TCP 客户端
class Client {
public:
    explicit Client(std::uint16_t port) : fd_(::socket(AF_INET, SOCK_STREAM, 0))
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connected_ = ::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
        timeval tv{5, 0};                   // 读最多等 5 秒，防止测试卡住
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    ~Client() { ::close(fd_); }
    bool connected() const { return connected_; }

    bool sendAll(const std::string& s)
    {
        std::size_t off = 0;
        while (off < s.size()) {
            ssize_t n = ::send(fd_, s.data() + off, s.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return false;
            off += static_cast<std::size_t>(n);
        }
        return true;
    }
    std::string recvExactly(std::size_t len)   // 读够 len 个字节，或者对方关闭、超时
    {
        std::string out;
        char buf[4096];
        while (out.size() < len) {
            ssize_t n = ::recv(fd_, buf, std::min(sizeof buf, len - out.size()), 0);
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
        }
        return out;
    }
    std::string recvUntilClose()
    {
        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = ::recv(fd_, buf, sizeof buf, 0)) > 0) out.append(buf, static_cast<std::size_t>(n));
        return out;
    }
    void shutdownWrite() { ::shutdown(fd_, SHUT_WR); }
    int fd() const { return fd_; }

private:
    int fd_;
    bool connected_ = false;
};

// 等到 pred() 为真，最多等 timeoutMs 毫秒
inline bool waitFor(const std::function<bool()>& pred, int timeoutMs = 3000)
{
    for (int i = 0; i < timeoutMs / 10; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

}  // namespace testutil
