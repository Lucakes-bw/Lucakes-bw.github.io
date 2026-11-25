#include "minilog/filter.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>

namespace minilog {

namespace {

bool isAsciiDigit(char c) noexcept {
  return c >= '0' && c <= '9';
}

bool isWordChar(char c) noexcept {
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0 || c == '_';
}

bool isTokenValChar(char c) noexcept {
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0 || c == '-' || c == '_' || c == '.' || c == '+' || c == '/';
}

bool matchTokenKey(std::string_view s, std::size_t pos) noexcept {
  if (pos + 6 > s.size()) return false;
  if (pos > 0 && isWordChar(s[pos - 1])) return false;
  static constexpr char kKey[] = "token=";
  for (std::size_t k = 0; k < 6; ++k) {
    if (static_cast<char>(std::tolower(static_cast<unsigned char>(s[pos + k]))) != kKey[k]) {
      return false;
    }
  }
  return true;
}

bool matchPhone11(std::string_view s, std::size_t pos) noexcept {
  if (pos + 11 > s.size()) return false;
  if (pos > 0 && isAsciiDigit(s[pos - 1])) return false;
  if (pos + 11 < s.size() && isAsciiDigit(s[pos + 11])) return false;
  if (s[pos] != '1') return false;
  if (s[pos + 1] < '3' || s[pos + 1] > '9') return false;
  for (std::size_t k = 2; k < 11; ++k) {
    if (!isAsciiDigit(s[pos + k])) return false;
  }
  return true;
}

}  // namespace

void FilterPipeline::add(FilterFn fn) {
  if (fn) filters_.push_back(std::move(fn));
}

bool FilterPipeline::apply(LogRecord& rec) const {
  for (const auto& fn : filters_) {
    if (!fn(rec)) return false;
  }
  return true;
}

FilterFn makeLevelFilter(Level minLevel) {
  return [minLevel](LogRecord& rec) {
    return static_cast<uint8_t>(rec.level) >= static_cast<uint8_t>(minLevel);
  };
}

FilterFn makeModuleFilter(std::vector<std::string> modules, bool allowList) {
  auto list = std::make_shared<std::vector<std::string>>(std::move(modules));
  return [list, allowList](LogRecord& rec) {
    const std::string_view name = rec.loggerName();
    const bool found =
        std::find(list->begin(), list->end(), name) != list->end();
    return allowList ? found : !found;
  };
}

FilterFn makeRateLimitFilter(uint32_t maxPerWindow, uint64_t windowMs) {
  struct State {
    std::mutex mtx;
    uint64_t windowStart = 0;
    uint32_t count = 0;
    bool initialized = false;
  };
  auto st = std::make_shared<State>();
  return [st, maxPerWindow, windowMs](LogRecord& rec) {
    if (maxPerWindow == 0) return false;
    std::lock_guard<std::mutex> lock(st->mtx);
    if (!st->initialized || rec.timestampMs < st->windowStart ||
        (windowMs > 0 && (rec.timestampMs - st->windowStart) >= windowMs)) {
      st->initialized = true;
      st->windowStart = rec.timestampMs;
      st->count = 1;
      return true;
    }
    if (st->count < maxPerWindow) {
      ++st->count;
      return true;
    }
    return false;
  };
}

FilterFn makeRedactFilter() {
  return [](LogRecord& rec) {
    const std::string_view msg = rec.message();
    std::string out;
    std::size_t copied = 0;
    bool dirty = false;

    std::size_t i = 0;
    while (i < msg.size()) {
      if (matchTokenKey(msg, i) && i + 6 < msg.size() && isTokenValChar(msg[i + 6])) {
        if (!dirty) {
          out.reserve(msg.size());
          dirty = true;
        }
        out.append(msg.data() + copied, i - copied);
        out.append(msg.data() + i, 6);
        out.append("***");
        i += 6;
        while (i < msg.size() && isTokenValChar(msg[i])) ++i;
        copied = i;
        continue;
      }
      if (matchPhone11(msg, i)) {
        if (!dirty) {
          out.reserve(msg.size());
          dirty = true;
        }
        out.append(msg.data() + copied, i - copied);
        out.append(msg.data() + i, 3);
        out.append("****");
        out.append(msg.data() + i + 7, 4);
        i += 11;
        copied = i;
        continue;
      }
      ++i;
    }

    if (dirty) {
      out.append(msg.data() + copied, msg.size() - copied);
      rec.setMessage(out);
    }
    return true;
  };
}

}  // namespace minilog
