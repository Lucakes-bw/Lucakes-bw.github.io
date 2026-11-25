#pragma once

#include "minilog/record.h"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace minilog {

// 预编译格式串：构造期一次性把 "%T [%L] [%n] %v" 编译成紧凑 Token 数组，热路径零解析开销
class PatternFormatter {
 public:
  struct LiteralTok { std::string text; };
  struct TimeTok {};
  struct LevelTok {};
  struct NameTok {};
  struct ThreadTok {};
  struct MsgTok {};

  using Token = std::variant<LiteralTok, TimeTok, LevelTok, NameTok, ThreadTok, MsgTok>;

  explicit PatternFormatter(std::string_view pattern = "[%T] [%L] [%n] %v");

  // 校验格式串合法性（供 LoggerBuilder 在构建期拦截未知占位符，如 %Q）
  static bool validate(std::string_view pattern, std::string& err);

  // 追加写入调用方复用的缓冲区 out
  void format(const LogRecord& rec, std::string& out) const;
  std::string formatString(const LogRecord& rec) const;

  const std::string& pattern() const noexcept { return pattern_; }
  std::size_t tokenCount() const noexcept { return tokens_.size(); }

 private:
  void compile(std::string_view pattern);

  std::string pattern_;
  std::vector<Token> tokens_;
};

}  // namespace minilog
