#pragma once

#include "minilog/level.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

namespace minilog {

// SBO 日志条目：<= 128 字节的消息直接内联在栈/环形槽位中（0 次堆分配），超长消息自动退回堆分配
struct LogRecord {
  static constexpr std::size_t kNameCap   = 32;
  static constexpr std::size_t kInlineCap = 128;

  uint64_t timestampMs = 0;
  Level level          = Level::Info;
  uint8_t nameLen_     = 0;
  uint32_t threadId    = 0;
  uint32_t msgLen_     = 0;
  char nameBuf_[kNameCap];
  char inlineBuf_[kInlineCap];
  std::unique_ptr<char[]> heapBuf_;

  LogRecord() noexcept = default;
  LogRecord(LogRecord&& other) noexcept
      : timestampMs(other.timestampMs),
        level(other.level),
        nameLen_(other.nameLen_),
        threadId(other.threadId),
        msgLen_(other.msgLen_),
        heapBuf_(std::move(other.heapBuf_)) {
    if (nameLen_ > 0) std::memcpy(nameBuf_, other.nameBuf_, nameLen_);
    if (!heapBuf_ && msgLen_ > 0) std::memcpy(inlineBuf_, other.inlineBuf_, msgLen_);
    other.nameLen_ = 0;
    other.msgLen_ = 0;
  }

  LogRecord& operator=(LogRecord&& other) noexcept {
    if (this == &other) return *this;
    timestampMs = other.timestampMs;
    level = other.level;
    nameLen_ = other.nameLen_;
    threadId = other.threadId;
    msgLen_ = other.msgLen_;
    heapBuf_ = std::move(other.heapBuf_);
    if (nameLen_ > 0) std::memcpy(nameBuf_, other.nameBuf_, nameLen_);
    if (!heapBuf_ && msgLen_ > 0) std::memcpy(inlineBuf_, other.inlineBuf_, msgLen_);
    other.nameLen_ = 0;
    other.msgLen_ = 0;
    return *this;
  }

  LogRecord(const LogRecord& other)
      : timestampMs(other.timestampMs),
        level(other.level),
        nameLen_(other.nameLen_),
        threadId(other.threadId),
        msgLen_(other.msgLen_) {
    if (nameLen_ > 0) std::memcpy(nameBuf_, other.nameBuf_, nameLen_);
    if (other.heapBuf_) {
      heapBuf_ = std::make_unique<char[]>(msgLen_ + 1);
      std::memcpy(heapBuf_.get(), other.heapBuf_.get(), msgLen_ + 1);
    } else if (msgLen_ > 0) {
      std::memcpy(inlineBuf_, other.inlineBuf_, msgLen_);
    }
  }

  LogRecord& operator=(const LogRecord& other) {
    if (this == &other) return *this;
    LogRecord tmp(other);
    *this = std::move(tmp);
    return *this;
  }

  bool isHeap() const noexcept { return static_cast<bool>(heapBuf_); }

  std::string_view loggerName() const noexcept {
    return std::string_view(nameBuf_, nameLen_);
  }

  void setLoggerName(std::string_view name) noexcept {
    const std::size_t n = std::min(name.size(), kNameCap - 1);
    if (n > 0) std::memcpy(nameBuf_, name.data(), n);
    nameBuf_[n] = '\0';
    nameLen_ = static_cast<uint8_t>(n);
  }

  std::string_view message() const noexcept {
    if (heapBuf_) return std::string_view(heapBuf_.get(), msgLen_);
    return std::string_view(inlineBuf_, msgLen_);
  }

  void setMessage(std::string_view msg) {
    msgLen_ = static_cast<uint32_t>(msg.size());
    if (msg.size() <= kInlineCap) {
      heapBuf_.reset();
      if (!msg.empty()) std::memcpy(inlineBuf_, msg.data(), msg.size());
      return;
    }
    heapBuf_ = std::make_unique<char[]>(msg.size() + 1);
    std::memcpy(heapBuf_.get(), msg.data(), msg.size());
    heapBuf_[msg.size()] = '\0';
  }
};

}  // namespace minilog
