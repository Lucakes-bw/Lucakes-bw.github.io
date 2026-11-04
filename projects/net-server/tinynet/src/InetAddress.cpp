#include "tinynet/InetAddress.h"

#include <arpa/inet.h>

#include "tinynet/Log.h"

namespace tinynet {

InetAddress::InetAddress(std::uint16_t port, bool loopbackOnly)
{
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    addr_.sin_port = htons(port);
}

InetAddress::InetAddress(const std::string& ip, std::uint16_t port)
{
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr) != 1) LOG_FATAL("不是合法的 IPv4 地址：%s", ip.c_str());
}

std::string InetAddress::toIp() const
{
    char buf[INET_ADDRSTRLEN] = "";
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    return buf;
}

std::string InetAddress::toIpPort() const { return toIp() + ":" + std::to_string(port()); }

std::uint16_t InetAddress::port() const { return ntohs(addr_.sin_port); }

}  // namespace tinynet
