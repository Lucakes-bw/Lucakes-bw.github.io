#include "minilog/sink.h"

#include <cstdio>

namespace minilog {

BaseSink::BaseSink(Level minLevel, std::string_view pattern)
    : minLevel_(minLevel), formatter_(pattern) {
  scratch_.reserve(256);
}

void BaseSink::log(const LogRecord& rec) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (static_cast<uint8_t>(rec.level) < static_cast<uint8_t>(minLevel_)) return;
  scratch_.clear();
  formatter_.format(rec, scratch_);
  scratch_.push_back('\n');
  writeImpl(scratch_, rec);
}

void BaseSink::flush() {
  std::lock_guard<std::mutex> lock(mtx_);
  flushImpl();
}

void BaseSink::setLevel(Level minLevel) {
  std::lock_guard<std::mutex> lock(mtx_);
  minLevel_ = minLevel;
}

Level BaseSink::level() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return minLevel_;
}

void BaseSink::setPattern(std::string_view pattern) {
  std::lock_guard<std::mutex> lock(mtx_);
  formatter_ = PatternFormatter(pattern);
}

ConsoleSink::ConsoleSink(Level minLevel, bool useStderr, std::string_view pattern)
    : BaseSink(minLevel, pattern), stream_(useStderr ? stderr : stdout) {}

void ConsoleSink::writeImpl(std::string_view formatted, const LogRecord&) {
  if (!formatted.empty()) {
    std::fwrite(formatted.data(), 1, formatted.size(), stream_);
  }
}

void ConsoleSink::flushImpl() {
  std::fflush(stream_);
}

FileSink::FileSink(std::string path, Level minLevel, std::size_t bufferBytes,
                   std::string_view pattern)
    : BaseSink(minLevel, pattern),
      path_(std::move(path)),
      bufferCap_(bufferBytes) {
  file_ = std::fopen(path_.c_str(), "ab");
  if (bufferCap_ > 0) buffer_.reserve(bufferCap_);
}

FileSink::~FileSink() {
  flushImpl();
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

void FileSink::flushBufferLocked() {
  if (file_ && !buffer_.empty()) {
    std::fwrite(buffer_.data(), 1, buffer_.size(), file_);
    buffer_.clear();
  }
}

void FileSink::writeImpl(std::string_view formatted, const LogRecord&) {
  if (!file_) return;
  if (bufferCap_ == 0) {
    std::fwrite(formatted.data(), 1, formatted.size(), file_);
    return;
  }
  if (buffer_.size() + formatted.size() > bufferCap_) {
    flushBufferLocked();
  }
  if (formatted.size() >= bufferCap_) {
    std::fwrite(formatted.data(), 1, formatted.size(), file_);
  } else {
    buffer_.append(formatted.data(), formatted.size());
  }
}

void FileSink::flushImpl() {
  flushBufferLocked();
  if (file_) std::fflush(file_);
}

RotatingFileSink::RotatingFileSink(std::string basePath,
                                   std::unique_ptr<RotationPolicy> policy,
                                   std::size_t maxFiles, Level minLevel,
                                   std::string_view pattern)
    : BaseSink(minLevel, pattern),
      basePath_(std::move(basePath)),
      policy_(std::move(policy)),
      maxFiles_(maxFiles) {
  openFreshLocked(0);
}

RotatingFileSink::~RotatingFileSink() {
  flushImpl();
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

std::size_t RotatingFileSink::rotateCount() const {
  return rotateCount_;
}

void RotatingFileSink::openFreshLocked(uint64_t nowMs) {
  file_ = std::fopen(basePath_.c_str(), "ab");
  bytesWritten_ = 0;
  linesWritten_ = 0;
  openedMs_ = nowMs;
  if (file_) {
    if (std::fseek(file_, 0, SEEK_END) == 0) {
      const long pos = std::ftell(file_);
      if (pos > 0) bytesWritten_ = static_cast<std::size_t>(pos);
    }
  }
}

void RotatingFileSink::rotateLocked(uint64_t nowMs) {
  if (file_) {
    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
  }

  auto indexedPath = [&](std::size_t idx) {
    return basePath_ + "." + std::to_string(idx);
  };

  if (maxFiles_ == 0) {
    std::remove(basePath_.c_str());
  } else {
    for (std::size_t i = maxFiles_; i >= 2; --i) {
      const std::string src = indexedPath(i - 1);
      const std::string dst = indexedPath(i);
      std::remove(dst.c_str());
      std::rename(src.c_str(), dst.c_str());
    }
    const std::string firstArchive = indexedPath(1);
    std::remove(firstArchive.c_str());
    std::rename(basePath_.c_str(), firstArchive.c_str());
  }

  ++rotateCount_;
  openFreshLocked(nowMs);
}

void RotatingFileSink::writeImpl(std::string_view formatted, const LogRecord& rec) {
  if (!file_) return;
  if (openedMs_ == 0 && rec.timestampMs != 0) openedMs_ = rec.timestampMs;

  if (linesWritten_ > 0 && policy_ &&
      policy_->shouldRotate(bytesWritten_ + formatted.size(), linesWritten_,
                            rec.timestampMs, openedMs_)) {
    rotateLocked(rec.timestampMs);
  }
  if (!file_) return;

  std::fwrite(formatted.data(), 1, formatted.size(), file_);
  bytesWritten_ += formatted.size();
  ++linesWritten_;
}

void RotatingFileSink::flushImpl() {
  if (file_) std::fflush(file_);
}

MemorySink::MemorySink(Level minLevel, std::string_view pattern)
    : BaseSink(minLevel, pattern) {}

void MemorySink::writeImpl(std::string_view formatted, const LogRecord&) {
  std::lock_guard<std::mutex> lock(dataMtx_);
  // 去掉末尾 '\n'，方便测试逐行断言
  if (!formatted.empty() && formatted.back() == '\n') {
    lines_.emplace_back(formatted.substr(0, formatted.size() - 1));
  } else {
    lines_.emplace_back(formatted);
  }
}

void MemorySink::flushImpl() {}

std::vector<std::string> MemorySink::lines() const {
  std::lock_guard<std::mutex> lock(dataMtx_);
  return lines_;
}

std::size_t MemorySink::count() const {
  std::lock_guard<std::mutex> lock(dataMtx_);
  return lines_.size();
}

void MemorySink::clear() {
  std::lock_guard<std::mutex> lock(dataMtx_);
  lines_.clear();
}

SinkFactory::SinkFactory() {
  registerType("console", [](const SinkSpec& spec, AnySink& out, std::string&) {
    out = AnySink(std::make_shared<ConsoleSink>(spec.minLevel, spec.useStderr, spec.pattern));
    return true;
  });
  registerType("file", [](const SinkSpec& spec, AnySink& out, std::string& err) {
    if (spec.path.empty()) {
      err = "file sink path must not be empty";
      return false;
    }
    auto sink = std::make_shared<FileSink>(spec.path, spec.minLevel, spec.bufferBytes, spec.pattern);
    if (!sink->isOpen()) {
      err = "failed to open log file: " + spec.path;
      return false;
    }
    out = AnySink(std::move(sink));
    return true;
  });
  registerType("rotating", [](const SinkSpec& spec, AnySink& out, std::string& err) {
    if (spec.path.empty()) {
      err = "rotating sink path must not be empty";
      return false;
    }
    std::unique_ptr<RotationPolicy> policy;
    if (spec.maxLines > 0) {
      policy = std::make_unique<LineCountRotation>(spec.maxLines);
    } else if (spec.intervalMs > 0) {
      policy = std::make_unique<TimeRotation>(spec.intervalMs);
    } else {
      policy = std::make_unique<SizeRotation>(spec.maxBytes);
    }
    auto sink = std::make_shared<RotatingFileSink>(
        spec.path, std::move(policy), spec.maxFiles, spec.minLevel, spec.pattern);
    if (!sink->isOpen()) {
      err = "failed to open rotating log file: " + spec.path;
      return false;
    }
    out = AnySink(std::move(sink));
    return true;
  });
  registerType("memory", [](const SinkSpec& spec, AnySink& out, std::string&) {
    out = AnySink(std::make_shared<MemorySink>(spec.minLevel, spec.pattern));
    return true;
  });
}

void SinkFactory::registerType(std::string typeName, CreatorFn fn) {
  creators_[std::move(typeName)] = std::move(fn);
}

bool SinkFactory::create(const SinkSpec& spec, AnySink& out, std::string& err) const {
  auto it = creators_.find(spec.type);
  if (it == creators_.end()) {
    err = "unknown sink type: " + spec.type;
    return false;
  }
  return it->second(spec, out, err);
}

bool SinkFactory::hasType(const std::string& typeName) const {
  return creators_.find(typeName) != creators_.end();
}

}  // namespace minilog
