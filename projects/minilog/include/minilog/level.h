#pragma once

#include <cstdint>
#include <string_view>

namespace minilog {

enum class Level : uint8_t {
  Trace = 0,
  Debug = 1,
  Info  = 2,
  Warn  = 3,
  Error = 4,
  Fatal = 5,
  Off   = 6
};

constexpr const char* levelName(Level lvl) noexcept {
  switch (lvl) {
    case Level::Trace: return "TRACE";
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
    case Level::Off:   return "OFF";
  }
  return "UNKNOWN";
}

inline bool levelFromString(std::string_view s, Level& out) noexcept {
  if (s == "TRACE" || s == "trace") { out = Level::Trace; return true; }
  if (s == "DEBUG" || s == "debug") { out = Level::Debug; return true; }
  if (s == "INFO"  || s == "info")  { out = Level::Info;  return true; }
  if (s == "WARN"  || s == "warn")  { out = Level::Warn;  return true; }
  if (s == "ERROR" || s == "error") { out = Level::Error; return true; }
  if (s == "FATAL" || s == "fatal") { out = Level::Fatal; return true; }
  if (s == "OFF"   || s == "off")   { out = Level::Off;   return true; }
  return false;
}

}  // namespace minilog
