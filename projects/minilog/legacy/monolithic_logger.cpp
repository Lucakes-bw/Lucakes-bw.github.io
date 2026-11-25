#include "monolithic_logger.h"

MonoLogger::~MonoLogger() {
  flush();
}

void MonoLogger::setLevel(Level l) {
  std::lock_guard<std::mutex> lock(mtx_);
  minLevel_ = l;
}

void MonoLogger::setOutputPath(const std::string& path) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (file_.is_open()) file_.close();
  outputPath_ = path;
  if (!outputPath_.empty()) {
    file_.open(outputPath_, std::ios::app);
  }
}

void MonoLogger::enableConsole(bool on) {
  std::lock_guard<std::mutex> lock(mtx_);
  consoleOn_ = on;
}

void MonoLogger::log(Level l, const std::string& module, const std::string& msg) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (l < minLevel_) return;

  std::ostringstream oss;
  oss << "[" << static_cast<int>(l) << "] [" << module << "] " << msg;
  const std::string line = oss.str();
  ++written_;
  checksum_ += line.size();

  if (benchDiscard_) {
    lastLine_ = line;
    return;
  }
  if (file_.is_open()) {
    file_ << line << '\n';
  } else {
    memBuffer_.push_back(line);
  }
  if (consoleOn_) {
    std::cout << line << '\n';
  }
}

void MonoLogger::flush() {
  std::lock_guard<std::mutex> lock(mtx_);
  if (file_.is_open()) file_.flush();
}

std::size_t MonoLogger::lineCount() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return written_;
}

void MonoLogger::setBenchDiscard(bool on) {
  std::lock_guard<std::mutex> lock(mtx_);
  benchDiscard_ = on;
}

std::size_t MonoLogger::checksum() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return checksum_;
}

void MonoLogger::resetForTest() {
  std::lock_guard<std::mutex> lock(mtx_);
  minLevel_ = Info;
  consoleOn_ = false;
  benchDiscard_ = false;
  if (file_.is_open()) file_.close();
  outputPath_.clear();
  memBuffer_.clear();
  lastLine_.clear();
  written_ = 0;
  checksum_ = 0;
}
