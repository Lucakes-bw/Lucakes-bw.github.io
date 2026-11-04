#pragma once

#include <map>
#include <string>

namespace tinynet::http {

struct HttpRequest {
    std::string method;                     // GET、POST……
    std::string path;                       // /index.html
    std::string query;                      // ? 后面的部分，没有就是空字符串
    std::string version;                    // HTTP/1.1
    std::map<std::string, std::string> headers;   // 头部的名字统一转成小写
    std::string body;

    std::string header(const std::string& lowerName) const
    {
        auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }
    bool keepAlive() const;                 // HTTP/1.1 默认保持连接，HTTP/1.0 默认关闭
};

}  // namespace tinynet::http
