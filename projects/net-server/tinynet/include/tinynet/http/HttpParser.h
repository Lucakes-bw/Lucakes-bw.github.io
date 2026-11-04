#pragma once

#include <cstddef>

#include "tinynet/http/HttpRequest.h"

namespace tinynet {
class Buffer;
}

namespace tinynet::http {

// 增量的 HTTP 请求解析器：数据一段一段地到达，每次有新数据就调用 parse，
// 它从 Buffer 中取走能解析的部分，记住解析到哪一步了，数据不够时等下一次。
class HttpParser {
public:
    enum class Result { kNeedMore, kGotRequest, kBadRequest };

    static constexpr std::size_t kMaxHeaderBytes = 8192;    // 请求行加头部的最大长度
    static constexpr std::size_t kMaxBodyBytes = 1 << 20;

    Result parse(Buffer* buf);
    const HttpRequest& request() const { return request_; }
    void reset();                           // 处理完一个请求以后调用，准备解析下一个

private:
    enum class State { kRequestLine, kHeaders, kBody };

    bool parseRequestLine(const char* begin, const char* end);
    bool parseHeader(const char* begin, const char* end);

    State state_ = State::kRequestLine;
    std::size_t headerBytes_ = 0;
    std::size_t bodyLength_ = 0;
    HttpRequest request_;
};

}  // namespace tinynet::http
