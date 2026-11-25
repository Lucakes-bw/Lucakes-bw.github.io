#include "minilog/logger.h"
#include "minilog/sink.h"
#include "test_common.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct ThrottledCountSink {
  std::shared_ptr<std::atomic<std::size_t>> written;
  std::chrono::microseconds delay;

  void write(const minilog::LogRecord&) {
    if (delay.count() > 0) std::this_thread::sleep_for(delay);
    written->fetch_add(1, std::memory_order_relaxed);
  }
};

void testSingleAndMultiThreadBlockDrain() {
  // 1) 单线程 10,000 条 + flush
  {
    auto mem = std::make_shared<minilog::MemorySink>(minilog::Level::Trace, "[%n] %v");
    auto res = minilog::LoggerBuilder()
                   .name("async1")
                   .level(minilog::Level::Trace)
                   .addSink(minilog::AnySink(mem))
                   .async(true, 1024, minilog::OverflowPolicy::Block)
                   .build();
    MINILOG_CHECK(static_cast<bool>(res));

    for (int i = 0; i < 10000; ++i) {
      res.logger->info("msg");
    }
    res.logger->flush();
    MINILOG_CHECK(mem->count() == 10000);
    MINILOG_CHECK(res.logger->acceptedCount() == 10000);
    MINILOG_CHECK(res.logger->droppedCount() == 0);
  }

  // 2) 4 线程共 40,000 条，不手动 flush，直接依赖 Logger RAII 析构优雅停机排空
  auto mem = std::make_shared<minilog::MemorySink>(minilog::Level::Trace, "%v");
  {
    auto res = minilog::LoggerBuilder()
                   .name("async4")
                   .level(minilog::Level::Trace)
                   .addSink(minilog::AnySink(mem))
                   .async(true, 512, minilog::OverflowPolicy::Block)
                   .build();
    MINILOG_CHECK(static_cast<bool>(res));

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
      threads.emplace_back([&, t]() {
        for (int i = 0; i < 10000; ++i) {
          res.logger->info("worker-" + std::to_string(t) + "-" + std::to_string(i));
        }
      });
    }
    for (auto& th : threads) th.join();
    // 作用域结束：res.logger 析构，AsyncWorker 自动排空全部 40,000 条再退出
  }
  MINILOG_CHECK(mem->count() == 40000);
}

void testOverflowPolicies() {
  // DropNewest
  {
    auto written = std::make_shared<std::atomic<std::size_t>>(0);
    uint64_t accepted = 0;
    uint64_t dropped = 0;
    {
      auto res = minilog::LoggerBuilder()
                     .name("drop")
                     .addSink(minilog::AnySink(ThrottledCountSink{written, std::chrono::microseconds(250)}))
                     .async(true, 8, minilog::OverflowPolicy::DropNewest)
                     .build();
      MINILOG_CHECK(static_cast<bool>(res));
      for (int i = 0; i < 200; ++i) res.logger->info("burst");
      accepted = res.logger->acceptedCount();
      dropped = res.logger->droppedCount();
    }
    MINILOG_CHECK(dropped > 0);
    MINILOG_CHECK(accepted + dropped == 200);
    MINILOG_CHECK(written->load() == accepted);
  }

  // OverwriteOldest
  {
    auto written = std::make_shared<std::atomic<std::size_t>>(0);
    uint64_t accepted = 0;
    uint64_t overwritten = 0;
    {
      auto res = minilog::LoggerBuilder()
                     .name("overwrite")
                     .addSink(minilog::AnySink(ThrottledCountSink{written, std::chrono::microseconds(250)}))
                     .async(true, 8, minilog::OverflowPolicy::OverwriteOldest)
                     .build();
      MINILOG_CHECK(static_cast<bool>(res));
      for (int i = 0; i < 200; ++i) res.logger->info("burst");
      accepted = res.logger->acceptedCount();
      overwritten = res.logger->overwrittenCount();
    }
    MINILOG_CHECK(accepted == 200);
    MINILOG_CHECK(overwritten > 0);
    MINILOG_CHECK(written->load() + overwritten == 200);
  }
}

}  // namespace

int main() {
  testSingleAndMultiThreadBlockDrain();
  testOverflowPolicies();
  std::printf("test_async passed\n");
  return 0;
}
