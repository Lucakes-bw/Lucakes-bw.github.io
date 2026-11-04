// 对照组：每个连接一个线程、阻塞 IO 的 HTTP 服务器，返回和 http_server 的 / 相同的响应
//   thread_per_conn [端口]
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

const std::string kResponse =
    "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 15\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n\r\nhello, tinynet\n";

void serve(int fd)
{
    std::string in;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        in.append(buf, static_cast<std::size_t>(n));
        std::string out;
        std::size_t end;
        while ((end = in.find("\r\n\r\n")) != std::string::npos) {   // 每个完整的请求回一个响应
            in.erase(0, end + 4);
            out += kResponse;
        }
        if (!out.empty() && ::write(fd, out.data(), out.size()) < 0) break;
    }
    ::close(fd);
}

}  // namespace

int main(int argc, char** argv)
{
    int port = argc > 1 ? std::atoi(argv[1]) : 8001;
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || ::listen(lfd, SOMAXCONN) < 0) {
        std::perror("bind/listen");
        return 1;
    }
    for (;;) {
        int fd = ::accept(lfd, nullptr, nullptr);
        if (fd < 0) continue;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
        std::thread(serve, fd).detach();
    }
}
