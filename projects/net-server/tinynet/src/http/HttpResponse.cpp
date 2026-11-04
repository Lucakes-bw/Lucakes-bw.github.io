#include "tinynet/http/HttpResponse.h"

#include "tinynet/Buffer.h"

namespace tinynet::http {

void HttpResponse::appendTo(Buffer* out) const
{
    out->append("HTTP/1.1 " + std::to_string(status_) + " " + reason_ + "\r\n");
    out->append(closeConnection_ ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
    out->append("Content-Length: " + std::to_string(body_.size()) + "\r\n");
    for (const auto& [name, value] : headers_) out->append(name + ": " + value + "\r\n");
    out->append("\r\n");
    out->append(body_);
}

}  // namespace tinynet::http
