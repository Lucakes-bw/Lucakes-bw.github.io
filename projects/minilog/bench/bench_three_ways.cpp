// 《实战（四）》实测：大泥球 vs 过度 GoF 炫技版 vs 克制现代 C++ 版（minilog）
#include "minilog/logger.h"
#include "minilog/sink.h"
#include "../legacy/monolithic_logger.h"
#define OVERENG_NO_MAIN
#include "../legacy/overengineered_logger.cpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string_view>
#include <vector>

static bool g_countAlloc = false;
static std::size_t g_allocCount = 0;

void* operator new(std::size_t sz) {
  if (g_countAlloc) ++g_allocCount;
  if (void* p = std::malloc(sz ? sz : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr int kCount  = 200000;
constexpr int kRounds = 5;
volatile std::size_t g_sink = 0;

class CountingSink final : public minilog::BaseSink {
 public:
  explicit CountingSink(minilog::Level lvl = minilog::Level::Trace) : BaseSink(lvl) {}
  std::size_t count() const noexcept { return count_; }
  std::size_t checksum() const noexcept { return checksum_; }
  void reset() noexcept { count_ = 0; checksum_ = 0; }
 private:
  void writeImpl(std::string_view formatted, const minilog::LogRecord&) override {
    ++count_;
    checksum_ += formatted.size();
  }
  void flushImpl() override {}
  std::size_t count_ = 0;
  std::size_t checksum_ = 0;
};

double median(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

int main() {
  constexpr std::string_view kMsg = "payment ok order=842103 latency=12ms";

  // 1. 大泥球单例版
  std::vector<double> tMono, tOver, tMini;
  std::size_t allocMono = 0, allocOver = 0, allocMini = 0;

  for (int r = 0; r < kRounds; ++r) {
    MonoLogger::instance().resetForTest();
    MonoLogger::instance().setLevel(MonoLogger::Info);
    MonoLogger::instance().setBenchDiscard(true);

    if (r == 0) { g_allocCount = 0; g_countAlloc = true; }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
      MonoLogger::instance().log(MonoLogger::Info, "payment", "payment ok order=842103 latency=12ms");
    }
    auto t1 = std::chrono::steady_clock::now();
    if (r == 0) { g_countAlloc = false; allocMono = g_allocCount; }
    g_sink += MonoLogger::instance().checksum();
    tMono.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }

  // 2. 过度 GoF 炫技版
  for (int r = 0; r < kRounds; ++r) {
    auto& over = overeng::OverEngineeredLogger::instance();
    overeng::StandardSuiteFactory factory;
    over.init(factory);

    if (r == 0) { g_allocCount = 0; g_countAlloc = true; }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
      over.log(overeng::Level::Info, "payment", "payment ok order=842103 latency=12ms");
    }
    auto t1 = std::chrono::steady_clock::now();
    if (r == 0) { g_countAlloc = false; allocOver = g_allocCount; }
    g_sink += over.checksum();
    tOver.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }

  // 3. 克制的现代 C++ 版 (minilog)
  for (int r = 0; r < kRounds; ++r) {
    auto cs = std::make_shared<CountingSink>(minilog::Level::Info);
    auto res = minilog::LoggerBuilder()
                   .name("payment")
                   .level(minilog::Level::Info)
                   .addMinLevelFilter(minilog::Level::Info)
                   .addModuleFilter({"payment"}, true)
                   .addRedactFilter()
                   .addSink(minilog::AnySink(cs))
                   .build();

    if (r == 0) { g_allocCount = 0; g_countAlloc = true; }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
      res.logger->logAt(minilog::Level::Info, 1000, kMsg);
    }
    auto t1 = std::chrono::steady_clock::now();
    if (r == 0) { g_countAlloc = false; allocMini = g_allocCount; }
    g_sink += cs->checksum();
    tMini.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }

  auto printRow = [](const char* label, double medMs, std::size_t allocs) {
    double nsPerOp = (medMs * 1e6) / kCount;
    std::printf("  %-24s | %7.2f ms | %7.1f ns/op | allocs=%zu (%.1f/op)\n",
                label, medMs, nsPerOp, allocs, static_cast<double>(allocs) / kCount);
  };

  std::printf("=== 三版本同台实测（%d 次调用，%d 轮中位数，-O2）===\n", kCount, kRounds);
  printRow("1. Monolithic 单例版", median(tMono), allocMono);
  printRow("2. Over-engineered GoF", median(tOver), allocOver);
  printRow("3. minilog 现代 C++ 版", median(tMini), allocMini);
  return 0;
}
