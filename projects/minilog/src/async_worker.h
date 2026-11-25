#pragma once

#include "minilog/async.h"
#include "minilog/record.h"
#include "minilog/sink.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace minilog {

class AsyncWorker {
 public:
  AsyncWorker(std::size_t capacity, OverflowPolicy overflow, std::vector<AnySink> sinks);
  ~AsyncWorker();

  AsyncWorker(const AsyncWorker&) = delete;
  AsyncWorker& operator=(const AsyncWorker&) = delete;

  bool enqueue(LogRecord&& rec);
  void flush();

  uint64_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }
  uint64_t overwrittenCount() const noexcept { return overwritten_.load(std::memory_order_relaxed); }

 private:
  void workerLoop();

  const std::size_t capacity_;
  const OverflowPolicy overflow_;
  std::vector<AnySink> sinks_;

  std::mutex mtx_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
  std::condition_variable flushedCv_;

  std::vector<LogRecord> ring_;
  std::size_t head_       = 0;
  std::size_t tail_       = 0;
  std::size_t count_      = 0;
  bool stopping_          = false;
  bool stopped_           = false;
  bool processing_        = false;
  uint64_t flushSeq_      = 0;
  uint64_t doneFlushSeq_  = 0;

  std::atomic<uint64_t> dropped_{0};
  std::atomic<uint64_t> overwritten_{0};

  std::thread worker_;
};

}  // namespace minilog
