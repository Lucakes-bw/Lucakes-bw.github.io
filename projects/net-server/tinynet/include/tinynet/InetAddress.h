#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <string>

namespace tinynet {

// IPv4 地址和端口
class InetAddress {
public:
    explicit InetAddress(std::uint16_t port = 0, bool loopbackOnly = false);
    InetAddress(const std::string& ip, std::uint16_t port);
    explicit InetAddress(const sockaddr_in& addr) : addr_(addr) {}

    std::string toIp() const;
    std::string toIpPort() const;           // 比如 127.0.0.1:8000
    std::uint16_t port() const;

    const sockaddr* sockAddr() const { return reinterpret_cast<const sockaddr*>(&addr_); }
    void setSockAddr(const sockaddr_in& addr) { addr_ = addr; }

private:
    sockaddr_in addr_{};
};

}  // namespace tinynet
