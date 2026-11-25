#include "minilog/pattern.h"

#include <cstring>

namespace minilog {

namespace {

void appendTwoDigits(uint32_t v, std::string& out) {
  char buf[2] = {static_cast<char>('0' + (v / 10) % 10),
                 static_cast<char>('0' + (v % 10))};
  out.append(buf, 2);
}

void appendThreeDigits(uint32_t v, std::string& out) {
  char buf[3] = {static_cast<char>('0' + (v / 100) % 10),
                 static_cast<char>('0' + (v / 10) % 10),
                 static_cast<char>('0' + (v % 10))};
  out.append(buf, 3);
}

void appendUint32(uint32_t v, std::string& out) {
  char buf[16];
  int pos = 16;
  do {
    buf[--pos] = static_cast<char>('0' + (v % 10));
    v /= 10;
  } while (v != 0);
  out.append(buf + pos, static_cast<std::size_t>(16 - pos));
}

void appendTime(uint64_t timestampMs, std::string& out) {
  const uint64_t totalSec = timestampMs / 1000ULL;
  const uint32_t ms       = static_cast<uint32_t>(timestampMs % 1000ULL);
  const uint32_t daySec   = static_cast<uint32_t>(totalSec % 86400ULL);
  const uint32_t hh       = daySec / 3600U;
  const uint32_t mm       = (daySec % 3600U) / 60U;
  const uint32_t ss       = daySec % 60U;

  appendTwoDigits(hh, out);
  out.push_back(':');
  appendTwoDigits(mm, out);
  out.push_back(':');
  appendTwoDigits(ss, out);
  out.push_back('.');
  appendThreeDigits(ms, out);
}

}  // namespace

PatternFormatter::PatternFormatter(std::string_view pattern) : pattern_(pattern) {
  compile(pattern);
}

bool PatternFormatter::validate(std::string_view pattern, std::string& err) {
  if (pattern.empty()) {
    err = "pattern must not be empty";
    return false;
  }
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] != '%') continue;
    if (i + 1 >= pattern.size()) {
      err = "pattern ends with dangling '%'";
      return false;
    }
    const char spec = pattern[++i];
    switch (spec) {
      case 'T':
      case 'L':
      case 'n':
      case 't':
      case 'v':
      case '%':
        break;
      default:
        err = std::string("unknown pattern specifier '%") + spec + "'";
        return false;
    }
  }
  return true;
}

void PatternFormatter::compile(std::string_view pattern) {
  tokens_.clear();
  std::string lit;
  auto flushLit = [&]() {
    if (!lit.empty()) {
      tokens_.emplace_back(LiteralTok{std::move(lit)});
      lit.clear();
    }
  };

  for (std::size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] != '%') {
      lit.push_back(pattern[i]);
      continue;
    }
    if (i + 1 >= pattern.size()) {
      lit.push_back('%');
      break;
    }
    const char spec = pattern[++i];
    if (spec == '%') {
      lit.push_back('%');
      continue;
    }
    flushLit();
    switch (spec) {
      case 'T': tokens_.emplace_back(TimeTok{}); break;
      case 'L': tokens_.emplace_back(LevelTok{}); break;
      case 'n': tokens_.emplace_back(NameTok{}); break;
      case 't': tokens_.emplace_back(ThreadTok{}); break;
      case 'v': tokens_.emplace_back(MsgTok{}); break;
      default:
        lit.push_back('%');
        lit.push_back(spec);
        break;
    }
  }
  flushLit();
}

void PatternFormatter::format(const LogRecord& rec, std::string& out) const {
  for (const Token& tok : tokens_) {
    std::visit(
        [&](const auto& t) {
          using T = std::decay_t<decltype(t)>;
          if constexpr (std::is_same_v<T, LiteralTok>) {
            out.append(t.text);
          } else if constexpr (std::is_same_v<T, TimeTok>) {
            appendTime(rec.timestampMs, out);
          } else if constexpr (std::is_same_v<T, LevelTok>) {
            out.append(levelName(rec.level));
          } else if constexpr (std::is_same_v<T, NameTok>) {
            const auto n = rec.loggerName();
            out.append(n.data(), n.size());
          } else if constexpr (std::is_same_v<T, ThreadTok>) {
            appendUint32(rec.threadId, out);
          } else if constexpr (std::is_same_v<T, MsgTok>) {
            const auto m = rec.message();
            out.append(m.data(), m.size());
          }
        },
        tok);
  }
}

std::string PatternFormatter::formatString(const LogRecord& rec) const {
  std::string out;
  out.reserve(64 + rec.message().size());
  format(rec, out);
  return out;
}

}  // namespace minilog
