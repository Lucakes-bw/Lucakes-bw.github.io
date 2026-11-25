#pragma once

// 严格编译防火墙：公开头文件不暴露 <fstream>/<mutex>/<thread>/<condition_variable>/<sstream>/<iostream>
#include "minilog/async.h"
#include "minilog/level.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace minilog {

struct LogRecord;
class AnySink;

// 外观 + Pimpl：sizeof(Logger) 恒为 8 字节，修改私有实现不触发调用方重编
class Logger {
 public:
  ~Logger();
  Logger(Logger&&) noexcept;
  Logger& operator=(Logger&&) noexcept;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  const std::string& name() const noexcept;
  Level level() const noexcept;
  void setLevel(Level lvl) noexcept;

  void log(Level lvl, std::string_view msg);
  void logAt(Level lvl, uint64_t timestampMs, std::string_view msg);

  void trace(std::string_view msg);
  void debug(std::string_view msg);
  void info(std::string_view msg);
  void warn(std::string_view msg);
  void error(std::string_view msg);
  void fatal(std::string_view msg);

  void flush();

  uint64_t acceptedCount() const noexcept;
  uint64_t droppedCount() const noexcept;
  uint64_t overwrittenCount() const noexcept;

 private:
  friend class LoggerBuilder;
  struct Impl;
  explicit Logger(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

struct BuildResult {
  std::unique_ptr<Logger> logger;
  std::string error;
  explicit operator bool() const noexcept { return logger != nullptr; }
};

// 建造者模式：链式装配参数并在 build() 统一校验，非法配置在发往磁盘/线程前拦下
class LoggerBuilder {
 public:
  LoggerBuilder();
  ~LoggerBuilder();
  LoggerBuilder(LoggerBuilder&&) noexcept;
  LoggerBuilder& operator=(LoggerBuilder&&) noexcept;

  LoggerBuilder(const LoggerBuilder&) = delete;
  LoggerBuilder& operator=(const LoggerBuilder&) = delete;

  LoggerBuilder& name(std::string name);
  LoggerBuilder& level(Level minLevel);
  LoggerBuilder& pattern(std::string pattern);

  LoggerBuilder& addConsoleSink(Level minLevel = Level::Trace, bool useStderr = false);
  LoggerBuilder& addFileSink(std::string path, Level minLevel = Level::Trace,
                             std::size_t bufferBytes = 65536);
  LoggerBuilder& addRotatingFileSink(std::string path, std::size_t maxBytes,
                                     std::size_t maxFiles = 3,
                                     Level minLevel = Level::Trace);
  LoggerBuilder& addSink(AnySink sink);

  LoggerBuilder& addFilter(std::function<bool(LogRecord&)> fn);
  LoggerBuilder& addMinLevelFilter(Level minLevel);
  LoggerBuilder& addModuleFilter(std::vector<std::string> modules, bool allowList);
  LoggerBuilder& addRateLimitFilter(uint32_t maxPerWindow, uint64_t windowMs);
  LoggerBuilder& addRedactFilter();

  LoggerBuilder& async(bool enabled, std::size_t queueCapacity = 4096,
                       OverflowPolicy overflow = OverflowPolicy::Block);

  BuildResult build();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

// 可注入实例注册表：代替全局单例，支持按名查找与测试完全隔离
class LoggerRegistry {
 public:
  LoggerRegistry();
  ~LoggerRegistry();
  LoggerRegistry(LoggerRegistry&&) noexcept;
  LoggerRegistry& operator=(LoggerRegistry&&) noexcept;

  LoggerRegistry(const LoggerRegistry&) = delete;
  LoggerRegistry& operator=(const LoggerRegistry&) = delete;

  bool registerLogger(std::shared_ptr<Logger> logger);
  void setLogger(std::shared_ptr<Logger> logger);
  std::shared_ptr<Logger> get(std::string_view name) const;
  bool drop(std::string_view name);
  void flushAll();
  void clear();
  std::size_t size() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace minilog
