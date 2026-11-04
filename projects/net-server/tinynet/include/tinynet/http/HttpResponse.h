#pragma once

#include <map>
#include <string>

namespace tinynet {
class Buffer;
}

namespace tinynet::http {

class HttpResponse {
public:
    explicit HttpResponse(bool close) : closeConnection_(close) {}

    void setStatus(int code, std::string reason)
    {
        status_ = code;
        reason_ = std::move(reason);
    }
    void setContentType(const std::string& type) { headers_["Content-Type"] = type; }
    void addHeader(const std::string& name, const std::string& value) { headers_[name] = value; }
    void setBody(std::string body) { body_ = std::move(body); }
    void setCloseConnection(bool on) { closeConnection_ = on; }
    bool closeConnection() const { return closeConnection_; }
    int status() const { return status_; }

    void appendTo(Buffer* out) const;       // 按 HTTP/1.1 的格式写进输出缓冲区

private:
    int status_ = 200;
    std::string reason_ = "OK";
    std::map<std::string, std::string> headers_;
    std::string body_;
    bool closeConnection_;
};

}  // namespace tinynet::http
