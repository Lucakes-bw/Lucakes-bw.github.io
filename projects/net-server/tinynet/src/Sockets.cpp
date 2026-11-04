#include "tinynet/Sockets.h"

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tinynet/Log.h"

namespace tinynet::sockets {

namespace {

void setOption(int fd, int level, int name, bool on)
{
    int v = on ? 1 : 0;
    if (::setsockopt(fd, level, name, &v, sizeof v) < 0) LOG_SYSERR("setsockopt(%d)", name);
}

InetAddress addressOf(int fd, int (*get)(int, sockaddr*, socklen_t*))
{
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    if (get(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) LOG_SYSERR("getsockname/getpeername");
    return InetAddress(addr);
}

}  // namespace

int createNonblockingOrDie()
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd < 0) LOG_SYSFATAL("socket");
    return fd;
}

void bindOrDie(int fd, const InetAddress& addr)
{
    if (::bind(fd, addr.sockAddr(), sizeof(sockaddr_in)) < 0) LOG_SYSFATAL("bind %s", addr.toIpPort().c_str());
}

void listenOrDie(int fd)
{
    if (::listen(fd, SOMAXCONN) < 0) LOG_SYSFATAL("listen");
}

int accept(int fd, InetAddress* peer)
{
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    int connfd = ::accept4(fd, reinterpret_cast<sockaddr*>(&addr), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0) peer->setSockAddr(addr);
    return connfd;
}

void close(int fd)
{
    if (::close(fd) < 0) LOG_SYSERR("close %d", fd);
}

void shutdownWrite(int fd)
{
    if (::shutdown(fd, SHUT_WR) < 0) LOG_SYSERR("shutdown %d", fd);
}

void setReuseAddr(int fd, bool on) { setOption(fd, SOL_SOCKET, SO_REUSEADDR, on); }
void setReusePort(int fd, bool on) { setOption(fd, SOL_SOCKET, SO_REUSEPORT, on); }
void setTcpNoDelay(int fd, bool on) { setOption(fd, IPPROTO_TCP, TCP_NODELAY, on); }
void setKeepAlive(int fd, bool on) { setOption(fd, SOL_SOCKET, SO_KEEPALIVE, on); }

int getSocketError(int fd)
{
    int err = 0;
    socklen_t len = sizeof err;
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return errno;
    return err;
}

InetAddress localAddress(int fd) { return addressOf(fd, ::getsockname); }
InetAddress peerAddress(int fd) { return addressOf(fd, ::getpeername); }

}  // namespace tinynet::sockets
