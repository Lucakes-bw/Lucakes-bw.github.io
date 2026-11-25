// 《实战（三）》实测：四条日志写入路径吞吐、前端延迟 P50/P99、堆分配与 CRTP vs 虚函数对比
#include "minilog/logger.h"
#include "minilog/pattern.h"
#include "minilog/record.h"
#include "minilog/sink.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

static std::atomic<bool> g_countThreadAlloc{false};
static std::atomic<std::size_t> g_heapAllocs{0};

void* operator new(std::size_t sz) {
  if (g_countThreadAlloc.load(std::memory_order_relaxed)) {
    g_heapAllocs.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* p = std::malloc(sz ? sz : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

// 50 字节日志消息：超过 libc++ std::string 22B SSO（必走堆分配），但完全装进 LogRecord 128B SBO
constexpr std::string_view kSampleMsg = "order=842103 status=paid latency=12ms sku=gpu-a100";

class TempDir {
 public:
  TempDir() {
    std::string tmpl = "/tmp/minilog_bench_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* p = ::mkdtemp(buf.data());
    dir_ = p ? std::filesystem::path(p) : (std::filesystem::temp_directory_path() / "minilog_bench");
    std::filesystem::create_directories(dir_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  std::string file(const std::string& name) const { return (dir_ / name).string(); }
 private:
  std::filesystem::path dir_;
};

// 基线 3：基于堆分配 std::string 的异步有界队列
class HeapStringAsyncLogger {
 public:
  struct Entry {
    uint64_t ts;
    minilog::Level lvl;
    std::string name;
    std::string msg;
  };

  HeapStringAsyncLogger(std::string path, std::size_t cap)
      : cap_(cap), ring_(cap), file_(std::move(path), minilog::Level::Trace, 65536) {
    worker_ = std::thread([this]() { run(); });
  }

  ~HeapStringAsyncLogger() {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      stopping_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void log(minilog::Level lvl, std::string_view name, std::string_view msg) {
    Entry e{1000, lvl, std::string(name), std::string(msg)};
    {
      std::unique_lock<std::mutex> lock(mtx_);
      notFull_.wait(lock, [this] { return count_ < cap_ || stopping_; });
      if (stopping_) return;
      ring_[tail_] = std::move(e);
      tail_ = (tail_ + 1) % cap_;
      ++count_;
    }
    notEmpty_.notify_one();
  }

 private:
  void run() {
    std::vector<Entry> batch;
    batch.reserve(64);
    minilog::LogRecord rec;
    for (;;) {
      batch.clear();
      bool exitNow = false;
      {
        std::unique_lock<std::mutex> lock(mtx_);
        notEmpty_.wait(lock, [this] { return count_ > 0 || stopping_; });
        std::size_t take = std::min(count_, static_cast<std::size_t>(64));
        for (std::size_t i = 0; i < take; ++i) {
          batch.push_back(std::move(ring_[head_]));
          head_ = (head_ + 1) % cap_;
          --count_;
        }
        if (count_ == 0 && stopping_) exitNow = true;
      }
      if (!batch.empty()) notFull_.notify_all();
      for (const auto& e : batch) {
        rec.timestampMs = e.ts;
        rec.level = e.lvl;
        rec.setLoggerName(e.name);
        rec.setMessage(e.msg);
        file_.log(rec);
      }
      if (exitNow) {
        file_.flush();
        return;
      }
    }
  }

  std::size_t cap_;
  std::vector<Entry> ring_;
  std::size_t head_ = 0, tail_ = 0, count_ = 0;
  bool stopping_ = false;
  std::mutex mtx_;
  std::condition_variable notEmpty_, notFull_;
  minilog::FileSink file_;
  std::thread worker_;
};

struct BenchStats {
  const char* name;
  double totalMs;
  double opsPerSec;
  double p50Ns;
  double p99Ns;
  std::size_t producerAllocs;
};

double percentile(std::vector<double>& v, double p) {
  std::sort(v.begin(), v.end());
  std::size_t idx = static_cast<std::size_t>(p * (v.size() - 1));
  return v[idx];
}

template <typename Fn>
BenchStats measurePath(const char* name, int threads, int perThread, Fn&& runOnce, bool countProducerAllocs) {
  constexpr int kRounds = 5;
  std::vector<double> msSamples;
  for (int r = 0; r < kRounds; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    runOnce(threads, perThread, nullptr, false);
    auto t1 = std::chrono::steady_clock::now();
    msSamples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  std::sort(msSamples.begin(), msSamples.end());
  double medMs = msSamples[kRounds / 2];

  // 单线程采样 10,000 次前端延迟 + 统计 10,000 次热路径堆分配（不含初始化）
  std::vector<double> lats;
  lats.reserve(10000);
  g_heapAllocs.store(0, std::memory_order_relaxed);
  runOnce(1, 10000, &lats, countProducerAllocs);
  g_countThreadAlloc.store(false, std::memory_order_relaxed);
  std::size_t allocs = g_heapAllocs.load(std::memory_order_relaxed);

  double totalOps = static_cast<double>(threads) * perThread;
  double opsPerSec = totalOps / (medMs / 1000.0);
  return {name, medMs, opsPerSec, percentile(lats, 0.50), percentile(lats, 0.99), allocs};
}

// CRTP vs 虚函数批量处理对比
volatile std::size_t g_batchSink = 0;

struct IVirtBatch {
  virtual ~IVirtBatch() = default;
  virtual std::size_t process(const minilog::LogRecord& rec) = 0;
};

struct VirtBatchCounter final : IVirtBatch {
  std::size_t process(const minilog::LogRecord& rec) override {
    return rec.message().size() + static_cast<std::size_t>(rec.level);
  }
};

template <typename Derived>
struct CrtpBatchBase {
  std::size_t runBatch(const std::vector<minilog::LogRecord>& recs) {
    std::size_t acc = 0;
    for (const auto& r : recs) {
      acc += static_cast<const Derived*>(this)->process(r);
    }
    return acc;
  }
};

struct CrtpBatchCounter final : CrtpBatchBase<CrtpBatchCounter> {
  std::size_t process(const minilog::LogRecord& rec) const {
    return rec.message().size() + static_cast<std::size_t>(rec.level);
  }
};

void benchCrtpVsVirtual() {
  constexpr int kN = 1000000;
  std::vector<minilog::LogRecord> recs(1024);
  for (auto& r : recs) {
    r.level = minilog::Level::Info;
    r.setLoggerName("pay");
    r.setMessage(kSampleMsg);
  }

  std::unique_ptr<IVirtBatch> v = std::make_unique<VirtBatchCounter>();
  CrtpBatchCounter c;

  std::vector<double> tv, tc;
  for (int r = 0; r < 7; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    std::size_t acc1 = 0;
    for (int k = 0; k < kN / 1024; ++k) {
      for (const auto& rec : recs) acc1 += v->process(rec);
    }
    auto t1 = std::chrono::steady_clock::now();
    g_batchSink += acc1;
    tv.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

    auto t2 = std::chrono::steady_clock::now();
    std::size_t acc2 = 0;
    for (int k = 0; k < kN / 1024; ++k) {
      acc2 += c.runBatch(recs);
    }
    auto t3 = std::chrono::steady_clock::now();
    g_batchSink += acc2;
    tc.push_back(std::chrono::duration<double, std::milli>(t3 - t2).count());
  }
  std::sort(tv.begin(), tv.end());
  std::sort(tc.begin(), tc.end());
  std::printf("\n=== 批量热循环：虚函数 vs CRTP（100 万条，7 轮中位数，-O2）===\n");
  std::printf("  Virtual sink : %.2f ms (sizeof=%zu B)\n", tv[3], sizeof(VirtBatchCounter));
  std::printf("  CRTP sink    : %.2f ms (sizeof=%zu B)\n", tc[3], sizeof(CrtpBatchCounter));
}

}  // namespace

int main() {
  TempDir dir;

  auto runSyncUnbuffered = [&](int threads, int perThread, std::vector<double>* lats, bool countAlloc) {
    const std::string path = dir.file("sync_unbuf.log");
    std::remove(path.c_str());
    std::ofstream ofs(path, std::ios::binary);
    std::mutex mtx;
    auto worker = [&]() {
      if (countAlloc) g_countThreadAlloc.store(true, std::memory_order_relaxed);
      for (int i = 0; i < perThread; ++i) {
        auto t0 = lats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        {
          std::lock_guard<std::mutex> lock(mtx);
          ofs << "[00:00:01.000] [INFO] [bench] " << kSampleMsg << '\n';
          ofs.flush();
        }
        if (lats) {
          auto t1 = std::chrono::steady_clock::now();
          lats->push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
      }
      if (countAlloc) g_countThreadAlloc.store(false, std::memory_order_relaxed);
    };
    if (threads == 1) {
      worker();
    } else {
      std::vector<std::thread> ts;
      for (int t = 0; t < threads; ++t) ts.emplace_back(worker);
      for (auto& th : ts) th.join();
    }
  };

  auto runSyncBuffered = [&](int threads, int perThread, std::vector<double>* lats, bool countAlloc) {
    const std::string path = dir.file("sync_buf.log");
    std::remove(path.c_str());
    auto res = minilog::LoggerBuilder()
                   .name("bench")
                   .level(minilog::Level::Trace)
                   .addFileSink(path, minilog::Level::Trace, 65536)
                   .build();
    auto worker = [&]() {
      if (countAlloc) g_countThreadAlloc.store(true, std::memory_order_relaxed);
      for (int i = 0; i < perThread; ++i) {
        auto t0 = lats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        res.logger->logAt(minilog::Level::Info, 1000, kSampleMsg);
        if (lats) {
          auto t1 = std::chrono::steady_clock::now();
          lats->push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
      }
      if (countAlloc) g_countThreadAlloc.store(false, std::memory_order_relaxed);
    };
    if (threads == 1) {
      worker();
    } else {
      std::vector<std::thread> ts;
      for (int t = 0; t < threads; ++t) ts.emplace_back(worker);
      for (auto& th : ts) th.join();
    }
    res.logger->flush();
  };

  auto runAsyncHeap = [&](int threads, int perThread, std::vector<double>* lats, bool countAlloc) {
    const std::string path = dir.file("async_heap.log");
    std::remove(path.c_str());
    HeapStringAsyncLogger logger(path, 16384);
    auto worker = [&]() {
      if (countAlloc) g_countThreadAlloc.store(true, std::memory_order_relaxed);
      for (int i = 0; i < perThread; ++i) {
        auto t0 = lats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        logger.log(minilog::Level::Info, "bench", kSampleMsg);
        if (lats) {
          auto t1 = std::chrono::steady_clock::now();
          lats->push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
      }
      if (countAlloc) g_countThreadAlloc.store(false, std::memory_order_relaxed);
    };
    if (threads == 1) {
      worker();
    } else {
      std::vector<std::thread> ts;
      for (int t = 0; t < threads; ++t) ts.emplace_back(worker);
      for (auto& th : ts) th.join();
    }
  };

  auto runAsyncSbo = [&](int threads, int perThread, std::vector<double>* lats, bool countAlloc) {
    const std::string path = dir.file("async_sbo.log");
    std::remove(path.c_str());
    auto res = minilog::LoggerBuilder()
                   .name("bench")
                   .level(minilog::Level::Trace)
                   .addFileSink(path, minilog::Level::Trace, 65536)
                   .async(true, 16384, minilog::OverflowPolicy::Block)
                   .build();
    auto worker = [&]() {
      if (countAlloc) g_countThreadAlloc.store(true, std::memory_order_relaxed);
      for (int i = 0; i < perThread; ++i) {
        auto t0 = lats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        res.logger->logAt(minilog::Level::Info, 1000, kSampleMsg);
        if (lats) {
          auto t1 = std::chrono::steady_clock::now();
          lats->push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
      }
      if (countAlloc) g_countThreadAlloc.store(false, std::memory_order_relaxed);
    };
    if (threads == 1) {
      worker();
    } else {
      std::vector<std::thread> ts;
      for (int t = 0; t < threads; ++t) ts.emplace_back(worker);
      for (auto& th : ts) th.join();
    }
  };

  auto printStats = [](const BenchStats& s) {
    std::printf("  %-26s | %7.2f ms | %10.0f ops/s | P50=%6.0f ns | P99=%7.0f ns | 10k allocs=%zu\n",
                s.name, s.totalMs, s.opsPerSec, s.p50Ns, s.p99Ns, s.producerAllocs);
  };

  std::printf("=== 单线程写文件（100,000 条，50B 消息，-O2）===\n");
  printStats(measurePath("1. Sync unbuffered", 1, 100000, runSyncUnbuffered, true));
  printStats(measurePath("2. Sync buffered (64KB)", 1, 100000, runSyncBuffered, true));
  printStats(measurePath("3. Async std::string", 1, 100000, runAsyncHeap, true));
  printStats(measurePath("4. Async minilog SBO", 1, 100000, runAsyncSbo, true));

  std::printf("\n=== 4 线程并发写文件（共 400,000 条，50B 消息，-O2）===\n");
  printStats(measurePath("1. Sync unbuffered", 4, 100000, runSyncUnbuffered, true));
  printStats(measurePath("2. Sync buffered (64KB)", 4, 100000, runSyncBuffered, true));
  printStats(measurePath("3. Async std::string", 4, 100000, runAsyncHeap, true));
  printStats(measurePath("4. Async minilog SBO", 4, 100000, runAsyncSbo, true));

  benchCrtpVsVirtual();
  return 0;
}
