#pragma once

#include "tinynet/InetAddress.h"

// 对套接字系统调用的薄封装。出错时，创建、绑定、监听这类「不该失败」的调用直接结束程序
namespace tinynet::sockets {

int createNonblockingOrDie();
void bindOrDie(int fd, const InetAddress& addr);
void listenOrDie(int fd);
int accept(int fd, InetAddress* peer);      // 返回非阻塞的新连接，失败返回 -1，errno 说明原因
void close(int fd);
void shutdownWrite(int fd);
void setReuseAddr(int fd, bool on);
void setReusePort(int fd, bool on);
void setTcpNoDelay(int fd, bool on);
void setKeepAlive(int fd, bool on);
int getSocketError(int fd);
InetAddress localAddress(int fd);
InetAddress peerAddress(int fd);

}  // namespace tinynet::sockets
