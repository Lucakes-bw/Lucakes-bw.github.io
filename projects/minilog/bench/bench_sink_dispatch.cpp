// 《实战（二）》实测：传统虚基类 unique_ptr<ISink> vs 48B SBO 类型擦除 AnySink
#include "minilog/record.h"
#include "minilog/sink.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
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

constexpr int kCount  = 1000000;
constexpr int kRounds = 7;
volatile long long g_acc = 0;

struct CounterSinkSmall {
  long long sum = 0;
  int factor = 1;
  void write(const minilog::LogRecord& rec) {
    sum += static_cast<long long>(rec.message().size()) * factor + static_cast<int>(rec.level);
  }
};

struct TagSinkSmall {
  long long sum = 0;
  int bias = 3;
  void write(const minilog::LogRecord& rec) {
    sum += static_cast<long long>(rec.loggerName().size()) + bias + static_cast<int>(rec.message().size());
  }
};

struct HashSinkSmall {
  long long sum = 0;
  uint64_t seed = 131;
  void write(const minilog::LogRecord& rec) {
    sum += static_cast<long long>((rec.timestampMs ^ seed) + rec.message().size());
  }
};

struct IVirtualSink {
  virtual ~IVirtualSink() = default;
  virtual void write(const minilog::LogRecord& rec) = 0;
  virtual long long total() const = 0;
};

struct VCounter final : IVirtualSink {
  CounterSinkSmall s;
  explicit VCounter(int f) : s{0, f} {}
  void write(const minilog::LogRecord& rec) override { s.write(rec); }
  long long total() const override { return s.sum; }
};

struct VTag final : IVirtualSink {
  TagSinkSmall s;
  explicit VTag(int b) : s{0, b} {}
  void write(const minilog::LogRecord& rec) override { s.write(rec); }
  long long total() const override { return s.sum; }
};

struct VHash final : IVirtualSink {
  HashSinkSmall s;
  explicit VHash(uint64_t seed) : s{0, seed} {}
  void write(const minilog::LogRecord& rec) override { s.write(rec); }
  long long total() const override { return s.sum; }
};

double median(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

int main() {
  minilog::LogRecord rec;
  rec.timestampMs = 123456;
  rec.level = minilog::Level::Info;
  rec.setLoggerName("pay");
  rec.setMessage("payment succeeded latency=12ms");

  std::vector<double> vCtor, vHot, aCtor, aHot;
  std::size_t vAllocs = 0, aAllocs = 0;

  // 热循环池：1024 个异构 Sink × 1000 轮
  constexpr int kPool = 1024;
  constexpr int kLoops = kCount / kPool;

  std::vector<std::unique_ptr<IVirtualSink>> vPool;
  std::vector<minilog::AnySink> aPool;
  vPool.reserve(kPool);
  aPool.reserve(kPool);
  for (int i = 0; i < kPool; ++i) {
    int k = i % 3;
    if (k == 0) {
      vPool.push_back(std::make_unique<VCounter>(i + 1));
      aPool.emplace_back(CounterSinkSmall{0, i + 1});
    } else if (k == 1) {
      vPool.push_back(std::make_unique<VTag>(i + 3));
      aPool.emplace_back(TagSinkSmall{0, i + 3});
    } else {
      vPool.push_back(std::make_unique<VHash>(static_cast<uint64_t>(i + 7)));
      aPool.emplace_back(HashSinkSmall{0, static_cast<uint64_t>(i + 7)});
    }
  }

  for (int r = 0; r < kRounds; ++r) {
    // 1. 传统虚基类构造+调用+析构
    if (r == 0) { g_allocCount = 0; g_countAlloc = true; }
    auto t0 = std::chrono::steady_clock::now();
    long long sum1 = 0;
    for (int i = 0; i < kCount; ++i) {
      int k = i % 3;
      std::unique_ptr<IVirtualSink> p;
      if (k == 0) p = std::make_unique<VCounter>(1);
      else if (k == 1) p = std::make_unique<VTag>(3);
      else p = std::make_unique<VHash>(131);
      p->write(rec);
      sum1 += p->total();
    }
    auto t1 = std::chrono::steady_clock::now();
    if (r == 0) { g_countAlloc = false; vAllocs = g_allocCount; }
    g_acc += sum1;
    vCtor.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kCount);

    // 2. AnySink (48B SBO) 构造+调用+析构
    if (r == 0) { g_allocCount = 0; g_countAlloc = true; }
    auto t2 = std::chrono::steady_clock::now();
    long long sum2 = 0;
    for (int i = 0; i < kCount; ++i) {
      int k = i % 3;
      minilog::AnySink s = (k == 0) ? minilog::AnySink(CounterSinkSmall{0, 1})
                         : (k == 1) ? minilog::AnySink(TagSinkSmall{0, 3})
                                    : minilog::AnySink(HashSinkSmall{0, 131});
      s.log(rec);
      sum2 += s.isInline() ? 1 : 0;
    }
    auto t3 = std::chrono::steady_clock::now();
    if (r == 0) { g_countAlloc = false; aAllocs = g_allocCount; }
    g_acc += sum2;
    aCtor.push_back(std::chrono::duration<double, std::nano>(t3 - t2).count() / kCount);

    // 3. 热循环容器派发
    auto h0 = std::chrono::steady_clock::now();
    for (int l = 0; l < kLoops; ++l) {
      for (auto& s : vPool) s->write(rec);
    }
    auto h1 = std::chrono::steady_clock::now();
    vHot.push_back(std::chrono::duration<double, std::nano>(h1 - h0).count() / (kLoops * kPool));

    auto h2 = std::chrono::steady_clock::now();
    for (int l = 0; l < kLoops; ++l) {
      for (auto& s : aPool) s.log(rec);
    }
    auto h3 = std::chrono::steady_clock::now();
    aHot.push_back(std::chrono::duration<double, std::nano>(h3 - h2).count() / (kLoops * kPool));
  }

  for (const auto& s : vPool) g_acc += s->total();

  std::printf("=== Sink 分派实测（%d 次，%d 轮中位数，-O2）===\n", kCount, kRounds);
  std::printf("  unique_ptr<ISink> : sizeof=%2zu B  allocs=%zu  构造+调用+析构=%.2f ns  热循环=%.2f ns\n",
              sizeof(std::unique_ptr<IVirtualSink>), vAllocs, median(vCtor), median(vHot));
  std::printf("  AnySink (48B SBO) : sizeof=%2zu B  allocs=%zu  构造+调用+析构=%.2f ns  热循环=%.2f ns\n",
              sizeof(minilog::AnySink), aAllocs, median(aCtor), median(aHot));
  return 0;
}
