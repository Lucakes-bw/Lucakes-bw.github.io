#include "tinynet/http/HttpParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

#include "tinynet/Buffer.h"

namespace tinynet::http {

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

}  // namespace

bool HttpRequest::keepAlive() const
{
    std::string conn = header("connection");
    std::transform(conn.begin(), conn.end(), conn.begin(), [](unsigned char c) { return std::tolower(c); });
    if (version == "HTTP/1.0") return conn == "keep-alive";
    return conn != "close";
}

void HttpParser::reset()
{
    state_ = State::kRequestLine;
    headerBytes_ = 0;
    bodyLength_ = 0;
    request_ = HttpRequest();
}

HttpParser::Result HttpParser::parse(Buffer* buf)
{
    while (true) {
        if (state_ == State::kBody) {
            if (buf->readableBytes() < bodyLength_) return Result::kNeedMore;
            request_.body = buf->retrieveAsString(bodyLength_);
            return Result::kGotRequest;
        }
        const char* crlf = buf->findCRLF();
        if (crlf == nullptr) {              // 一行还没有收完
            if (headerBytes_ + buf->readableBytes() > kMaxHeaderBytes) return Result::kBadRequest;
            return Result::kNeedMore;
        }
        headerBytes_ += static_cast<std::size_t>(crlf - buf->peek()) + 2;
        if (headerBytes_ > kMaxHeaderBytes) return Result::kBadRequest;

        if (state_ == State::kRequestLine) {
            if (!parseRequestLine(buf->peek(), crlf)) return Result::kBadRequest;
            state_ = State::kHeaders;
        } else if (crlf == buf->peek()) {   // 空行：头部结束
            buf->retrieveUntil(crlf + 2);
            std::string len = request_.header("content-length");
            if (len.empty()) return Result::kGotRequest;
            char* end = nullptr;
            unsigned long n = std::strtoul(len.c_str(), &end, 10);
            if (*end != '\0' || n > kMaxBodyBytes) return Result::kBadRequest;
            bodyLength_ = n;
            state_ = State::kBody;
            continue;
        } else if (!parseHeader(buf->peek(), crlf)) {
            return Result::kBadRequest;
        }
        buf->retrieveUntil(crlf + 2);
    }
}

// GET /path?query HTTP/1.1
bool HttpParser::parseRequestLine(const char* begin, const char* end)
{
    std::string_view line(begin, static_cast<std::size_t>(end - begin));
    std::size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos || sp1 == 0) return false;
    std::size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || sp2 == sp1 + 1) return false;
    request_.method = std::string(line.substr(0, sp1));
    std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::size_t q = target.find('?');
    request_.path = std::string(target.substr(0, q));
    if (q != std::string_view::npos) request_.query = std::string(target.substr(q + 1));
    request_.version = std::string(line.substr(sp2 + 1));
    return request_.version == "HTTP/1.1" || request_.version == "HTTP/1.0";
}

// Name: value
bool HttpParser::parseHeader(const char* begin, const char* end)
{
    std::string_view line(begin, static_cast<std::size_t>(end - begin));
    std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    std::string name(trim(line.substr(0, colon)));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    request_.headers[name] = std::string(trim(line.substr(colon + 1)));
    return true;
}

}  // namespace tinynet::http
