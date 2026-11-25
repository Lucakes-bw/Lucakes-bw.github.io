#include "async_worker.h"

#include <algorithm>
#include <utility>

namespace minilog {

AsyncWorker::AsyncWorker(std::size_t capacity, OverflowPolicy overflow,
                         std::vector<AnySink> sinks)
    : capacity_(capacity > 0 ? capacity : 1),
      overflow_(overflow),
      sinks_(std::move(sinks)),
      ring_(capacity_) {
  worker_ = std::thread(&AsyncWorker::workerLoop, this);
}

AsyncWorker::~AsyncWorker() {
  {
    std::lock_guard<std::mutex> lock(mtx_);
    stopping_ = true;
  }
  notEmpty_.notify_all();
  notFull_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool AsyncWorker::enqueue(LogRecord&& rec) {
  bool needWake = false;
  {
    std::unique_lock<std::mutex> lock(mtx_);
    if (overflow_ == OverflowPolicy::Block) {
      notFull_.wait(lock, [this] { return count_ < capacity_ || stopping_; });
      if (stopping_) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      needWake = (count_ == 0);
      ring_[tail_] = std::move(rec);
      tail_ = (tail_ + 1) % capacity_;
      ++count_;
    } else if (overflow_ == OverflowPolicy::DropNewest) {
      if (stopping_ || count_ >= capacity_) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      needWake = (count_ == 0);
      ring_[tail_] = std::move(rec);
      tail_ = (tail_ + 1) % capacity_;
      ++count_;
    } else {
      if (stopping_) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      if (count_ >= capacity_) {
        ring_[head_] = std::move(rec);
        head_ = (head_ + 1) % capacity_;
        tail_ = head_;
        overwritten_.fetch_add(1, std::memory_order_relaxed);
      } else {
        needWake = (count_ == 0);
        ring_[tail_] = std::move(rec);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
      }
    }
  }
  if (needWake) notEmpty_.notify_one();
  return true;
}

void AsyncWorker::flush() {
  std::unique_lock<std::mutex> lock(mtx_);
  if (stopped_) return;
  const uint64_t target = ++flushSeq_;
  notEmpty_.notify_one();
  flushedCv_.wait(lock, [this, target] {
    return doneFlushSeq_ >= target || stopped_;
  });
}

void AsyncWorker::workerLoop() {
  constexpr std::size_t kBatchCap = 64;
  std::vector<LogRecord> batch;
  batch.reserve(kBatchCap);

  for (;;) {
    batch.clear();
    bool hitEmpty = false;
    uint64_t seenFlush = 0;
    bool exitAfterFlush = false;

    {
      std::unique_lock<std::mutex> lock(mtx_);
      notEmpty_.wait(lock, [this] {
        return count_ > 0 || stopping_ || flushSeq_ > doneFlushSeq_;
      });

      const std::size_t take = std::min(count_, kBatchCap);
      for (std::size_t i = 0; i < take; ++i) {
        batch.push_back(std::move(ring_[head_]));
        head_ = (head_ + 1) % capacity_;
        --count_;
      }

      if (count_ == 0) {
        hitEmpty = true;
        seenFlush = flushSeq_;
        if (stopping_) exitAfterFlush = true;
      }
      processing_ = true;
    }

    if (!batch.empty()) notFull_.notify_all();

    for (const auto& rec : batch) {
      for (auto& s : sinks_) s.log(rec);
    }

    const bool needSinkFlush = exitAfterFlush || (hitEmpty && seenFlush > doneFlushSeq_);
    if (needSinkFlush) {
      for (auto& s : sinks_) s.flush();
    }

    {
      std::lock_guard<std::mutex> lock(mtx_);
      processing_ = false;
      if (hitEmpty && seenFlush > doneFlushSeq_) {
        doneFlushSeq_ = seenFlush;
      }
      if (exitAfterFlush) {
        stopped_ = true;
        flushedCv_.notify_all();
        return;
      }
      if (hitEmpty) flushedCv_.notify_all();
    }
  }
}

}  // namespace minilog
