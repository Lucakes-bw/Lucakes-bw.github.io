#pragma once

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 极简的日志：输出到标准错误。SYS 系列会附上 errno 的说明，FATAL 输出以后结束程序
namespace tinynet::detail {

[[gnu::format(printf, 5, 6)]] inline void log(const char* level, const char* file, int line, int err,
                                              const char* fmt, ...)
{
    std::fprintf(stderr, "[%s] %s:%d ", level, file, line);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (err != 0) std::fprintf(stderr, "：%s", std::strerror(err));
    std::fputc('\n', stderr);
}

}  // namespace tinynet::detail

#define LOG_INFO(...) ::tinynet::detail::log("INFO", __FILE__, __LINE__, 0, __VA_ARGS__)
#define LOG_ERROR(...) ::tinynet::detail::log("ERROR", __FILE__, __LINE__, 0, __VA_ARGS__)
#define LOG_SYSERR(...) ::tinynet::detail::log("ERROR", __FILE__, __LINE__, errno, __VA_ARGS__)
#define LOG_FATAL(...) (::tinynet::detail::log("FATAL", __FILE__, __LINE__, 0, __VA_ARGS__), std::abort())
#define LOG_SYSFATAL(...) (::tinynet::detail::log("FATAL", __FILE__, __LINE__, errno, __VA_ARGS__), std::abort())
