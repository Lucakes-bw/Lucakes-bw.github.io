#include "minilog/logger.h"

#include "async_worker.h"
#include "minilog/filter.h"
#include "minilog/pattern.h"
#include "minilog/record.h"
#include "minilog/sink.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace minilog {

namespace {

uint64_t nowEpochMs() noexcept {
  const auto d = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

uint32_t currentThreadId() noexcept {
  thread_local const uint32_t cached = static_cast<uint32_t>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return cached;
}

}  // namespace

// -------------------- Logger::Impl --------------------

struct Logger::Impl {
  std::string name;
  std::atomic<uint8_t> level{static_cast<uint8_t>(Level::Info)};
  FilterPipeline filters;
  std::vector<AnySink> syncSinks;
  std::mutex syncMtx;
  std::unique_ptr<AsyncWorker> asyncWorker;
  std::atomic<uint64_t> accepted{0};
  std::atomic<uint64_t> syncDropped{0};
#ifdef MINILOG_ADD_PRIVATE_FIELD
  int retryCount_ = 0;
#endif

  ~Impl() {
    if (asyncWorker) {
      asyncWorker.reset();
    } else {
      std::lock_guard<std::mutex> lock(syncMtx);
      for (auto& s : syncSinks) s.flush();
    }
  }
};

Logger::Logger(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Logger::~Logger() = default;
Logger::Logger(Logger&&) noexcept = default;
Logger& Logger::operator=(Logger&&) noexcept = default;

const std::string& Logger::name() const noexcept {
  return impl_->name;
}

Level Logger::level() const noexcept {
  return static_cast<Level>(impl_->level.load(std::memory_order_relaxed));
}

void Logger::setLevel(Level lvl) noexcept {
  impl_->level.store(static_cast<uint8_t>(lvl), std::memory_order_relaxed);
}

void Logger::log(Level lvl, std::string_view msg) {
  if (static_cast<uint8_t>(lvl) < impl_->level.load(std::memory_order_relaxed)) return;
  logAt(lvl, nowEpochMs(), msg);
}

void Logger::logAt(Level lvl, uint64_t timestampMs, std::string_view msg) {
  if (static_cast<uint8_t>(lvl) < impl_->level.load(std::memory_order_relaxed)) return;

  LogRecord rec;
  rec.timestampMs = timestampMs;
  rec.level = lvl;
  rec.threadId = currentThreadId();
  rec.setLoggerName(impl_->name);
  rec.setMessage(msg);

  if (!impl_->filters.apply(rec)) {
    impl_->syncDropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  if (impl_->asyncWorker) {
    if (impl_->asyncWorker->enqueue(std::move(rec))) {
      impl_->accepted.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }

  impl_->accepted.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(impl_->syncMtx);
  for (auto& s : impl_->syncSinks) s.log(rec);
}

void Logger::trace(std::string_view msg) { log(Level::Trace, msg); }
void Logger::debug(std::string_view msg) { log(Level::Debug, msg); }
void Logger::info(std::string_view msg)  { log(Level::Info, msg); }
void Logger::warn(std::string_view msg)  { log(Level::Warn, msg); }
void Logger::error(std::string_view msg) { log(Level::Error, msg); }
void Logger::fatal(std::string_view msg) { log(Level::Fatal, msg); }

void Logger::flush() {
  if (impl_->asyncWorker) {
    impl_->asyncWorker->flush();
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->syncMtx);
  for (auto& s : impl_->syncSinks) s.flush();
}

uint64_t Logger::acceptedCount() const noexcept {
  return impl_->accepted.load(std::memory_order_relaxed);
}

uint64_t Logger::droppedCount() const noexcept {
  uint64_t total = impl_->syncDropped.load(std::memory_order_relaxed);
  if (impl_->asyncWorker) total += impl_->asyncWorker->droppedCount();
  return total;
}

uint64_t Logger::overwrittenCount() const noexcept {
  return impl_->asyncWorker ? impl_->asyncWorker->overwrittenCount() : 0;
}

// -------------------- LoggerBuilder --------------------

struct LoggerBuilder::State {
  std::string name;
  Level level = Level::Info;
  std::string pattern = "[%T] [%L] [%n] %v";
  std::vector<SinkSpec> sinkSpecs;
  std::vector<AnySink> customSinks;
  std::vector<FilterFn> filters;
  AsyncConfig async;
};

LoggerBuilder::LoggerBuilder() : state_(std::make_unique<State>()) {}
LoggerBuilder::~LoggerBuilder() = default;
LoggerBuilder::LoggerBuilder(LoggerBuilder&&) noexcept = default;
LoggerBuilder& LoggerBuilder::operator=(LoggerBuilder&&) noexcept = default;

LoggerBuilder& LoggerBuilder::name(std::string name) {
  state_->name = std::move(name);
  return *this;
}

LoggerBuilder& LoggerBuilder::level(Level minLevel) {
  state_->level = minLevel;
  return *this;
}

LoggerBuilder& LoggerBuilder::pattern(std::string pattern) {
  state_->pattern = std::move(pattern);
  return *this;
}

LoggerBuilder& LoggerBuilder::addConsoleSink(Level minLevel, bool useStderr) {
  SinkSpec spec;
  spec.type = "console";
  spec.minLevel = minLevel;
  spec.useStderr = useStderr;
  state_->sinkSpecs.push_back(std::move(spec));
  return *this;
}

LoggerBuilder& LoggerBuilder::addFileSink(std::string path, Level minLevel,
                                          std::size_t bufferBytes) {
  SinkSpec spec;
  spec.type = "file";
  spec.path = std::move(path);
  spec.minLevel = minLevel;
  spec.bufferBytes = bufferBytes;
  state_->sinkSpecs.push_back(std::move(spec));
  return *this;
}

LoggerBuilder& LoggerBuilder::addRotatingFileSink(std::string path, std::size_t maxBytes,
                                                  std::size_t maxFiles, Level minLevel) {
  SinkSpec spec;
  spec.type = "rotating";
  spec.path = std::move(path);
  spec.maxBytes = maxBytes;
  spec.maxFiles = maxFiles;
  spec.minLevel = minLevel;
  state_->sinkSpecs.push_back(std::move(spec));
  return *this;
}

LoggerBuilder& LoggerBuilder::addSink(AnySink sink) {
  state_->customSinks.push_back(std::move(sink));
  return *this;
}

LoggerBuilder& LoggerBuilder::addFilter(std::function<bool(LogRecord&)> fn) {
  state_->filters.push_back(std::move(fn));
  return *this;
}

LoggerBuilder& LoggerBuilder::addMinLevelFilter(Level minLevel) {
  state_->filters.push_back(makeLevelFilter(minLevel));
  return *this;
}

LoggerBuilder& LoggerBuilder::addModuleFilter(std::vector<std::string> modules,
                                              bool allowList) {
  state_->filters.push_back(makeModuleFilter(std::move(modules), allowList));
  return *this;
}

LoggerBuilder& LoggerBuilder::addRateLimitFilter(uint32_t maxPerWindow, uint64_t windowMs) {
  state_->filters.push_back(makeRateLimitFilter(maxPerWindow, windowMs));
  return *this;
}

LoggerBuilder& LoggerBuilder::addRedactFilter() {
  state_->filters.push_back(makeRedactFilter());
  return *this;
}

LoggerBuilder& LoggerBuilder::async(bool enabled, std::size_t queueCapacity,
                                    OverflowPolicy overflow) {
  state_->async.enabled = enabled;
  state_->async.queueCapacity = queueCapacity;
  state_->async.overflow = overflow;
  return *this;
}

BuildResult LoggerBuilder::build() {
  if (state_->name.empty()) {
    return {nullptr, "logger name must not be empty"};
  }
  std::string patErr;
  if (!PatternFormatter::validate(state_->pattern, patErr)) {
    return {nullptr, patErr};
  }
  if (state_->sinkSpecs.empty() && state_->customSinks.empty()) {
    return {nullptr, "logger requires at least one sink"};
  }
  if (state_->async.enabled && state_->async.queueCapacity == 0) {
    return {nullptr, "async queueCapacity must be > 0"};
  }
  for (const auto& s : state_->sinkSpecs) {
    if ((s.type == "file" || s.type == "rotating") && s.path.empty()) {
      return {nullptr, s.type + " sink path must not be empty"};
    }
    if (s.type == "rotating" && s.maxBytes == 0 && s.maxLines == 0 && s.intervalMs == 0) {
      return {nullptr, "rotating sink maxBytes must be > 0"};
    }
  }

  SinkFactory factory;
  std::vector<AnySink> builtSinks;
  builtSinks.reserve(state_->sinkSpecs.size() + state_->customSinks.size());

  for (auto& spec : state_->sinkSpecs) {
    spec.pattern = state_->pattern;
    AnySink sink;
    std::string err;
    if (!factory.create(spec, sink, err)) {
      return {nullptr, err};
    }
    builtSinks.push_back(std::move(sink));
  }
  for (auto& cs : state_->customSinks) {
    if (!cs) return {nullptr, "custom sink must not be empty"};
    builtSinks.push_back(std::move(cs));
  }

  auto impl = std::make_unique<Logger::Impl>();
  impl->name = state_->name;
  impl->level.store(static_cast<uint8_t>(state_->level), std::memory_order_relaxed);
  for (auto& f : state_->filters) impl->filters.add(std::move(f));

  if (state_->async.enabled) {
    impl->asyncWorker = std::make_unique<AsyncWorker>(
        state_->async.queueCapacity, state_->async.overflow, std::move(builtSinks));
  } else {
    impl->syncSinks = std::move(builtSinks);
  }

  return {std::unique_ptr<Logger>(new Logger(std::move(impl))), ""};
}

// -------------------- LoggerRegistry --------------------

struct LoggerRegistry::Impl {
  mutable std::mutex mtx;
  std::unordered_map<std::string, std::shared_ptr<Logger>> loggers;
};

LoggerRegistry::LoggerRegistry() : impl_(std::make_unique<Impl>()) {}
LoggerRegistry::~LoggerRegistry() = default;
LoggerRegistry::LoggerRegistry(LoggerRegistry&&) noexcept = default;
LoggerRegistry& LoggerRegistry::operator=(LoggerRegistry&&) noexcept = default;

bool LoggerRegistry::registerLogger(std::shared_ptr<Logger> logger) {
  if (!logger) return false;
  std::lock_guard<std::mutex> lock(impl_->mtx);
  return impl_->loggers.emplace(logger->name(), std::move(logger)).second;
}

void LoggerRegistry::setLogger(std::shared_ptr<Logger> logger) {
  if (!logger) return;
  std::lock_guard<std::mutex> lock(impl_->mtx);
  impl_->loggers[logger->name()] = std::move(logger);
}

std::shared_ptr<Logger> LoggerRegistry::get(std::string_view name) const {
  std::lock_guard<std::mutex> lock(impl_->mtx);
  auto it = impl_->loggers.find(std::string(name));
  return it == impl_->loggers.end() ? nullptr : it->second;
}

bool LoggerRegistry::drop(std::string_view name) {
  std::lock_guard<std::mutex> lock(impl_->mtx);
  return impl_->loggers.erase(std::string(name)) > 0;
}

void LoggerRegistry::flushAll() {
  std::vector<std::shared_ptr<Logger>> copy;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    copy.reserve(impl_->loggers.size());
    for (const auto& kv : impl_->loggers) copy.push_back(kv.second);
  }
  for (const auto& l : copy) {
    if (l) l->flush();
  }
}

void LoggerRegistry::clear() {
  std::lock_guard<std::mutex> lock(impl_->mtx);
  impl_->loggers.clear();
}

std::size_t LoggerRegistry::size() const {
  std::lock_guard<std::mutex> lock(impl_->mtx);
  return impl_->loggers.size();
}

}  // namespace minilog
