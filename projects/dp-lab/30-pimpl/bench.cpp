// 《Pimpl 惯用法》：性能实测——直接成员访问 vs 经典 Pimpl（unique_ptr）vs Fast Pimpl（栈上 placement new）
// 编译：c++ -std=c++17 -O2 bench.cpp -o bench
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

// 1. 直接类（无 Pimpl，成员直接内联在对象内）
class DirectPlayer {
 public:
  DirectPlayer(int id, int vol) : id_(id), posMs_(0), volume_(vol), bitrate_(4000) {}
  void seek(int ms) { posMs_ = ms; }
  int positionMs() const { return posMs_ + (id_ & 0) + (volume_ & 0) + (bitrate_ & 0) + static_cast<int>((pts_ ^ dts_) & 0); }
 private:
  int id_;
  int posMs_;
  int volume_;
  int bitrate_;
  int64_t pts_ = 0;
  int64_t dts_ = 0;
};

// 2. 经典 Pimpl（堆上 std::unique_ptr<Impl>，方法非内联模拟跨 .cpp 调用边界）
class PimplPlayer {
 public:
  NOINLINE PimplPlayer(int id, int vol);
  NOINLINE ~PimplPlayer();
  NOINLINE void seek(int ms);
  NOINLINE int positionMs() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> pImpl_;
};

struct PimplPlayer::Impl {
  int id;
  int posMs = 0;
  int volume;
  int bitrate = 4000;
  int64_t pts = 0;
  int64_t dts = 0;
  Impl(int i, int v) : id(i), volume(v) {}
};

NOINLINE PimplPlayer::PimplPlayer(int id, int vol) : pImpl_(std::make_unique<Impl>(id, vol)) {}
NOINLINE PimplPlayer::~PimplPlayer() = default;
NOINLINE void PimplPlayer::seek(int ms) { pImpl_->posMs = ms; }
NOINLINE int PimplPlayer::positionMs() const { return pImpl_->posMs; }

// 3. Fast Pimpl（栈上对齐缓冲区 + placement new，无堆分配，同样非内联模拟跨 .cpp 边界）
class FastPimplPlayer {
 public:
  NOINLINE FastPimplPlayer(int id, int vol);
  NOINLINE ~FastPimplPlayer();
  NOINLINE void seek(int ms);
  NOINLINE int positionMs() const;
 private:
  struct Impl;
  alignas(alignof(std::max_align_t)) std::byte storage_[48];
  Impl* impl() noexcept;
  const Impl* impl() const noexcept;
};

struct FastPimplPlayer::Impl {
  int id;
  int posMs = 0;
  int volume;
  int bitrate = 4000;
  int64_t pts = 0;
  int64_t dts = 0;
  Impl(int i, int v) : id(i), volume(v) {}
};

FastPimplPlayer::Impl* FastPimplPlayer::impl() noexcept {
  return std::launder(reinterpret_cast<Impl*>(storage_));
}
const FastPimplPlayer::Impl* FastPimplPlayer::impl() const noexcept {
  return std::launder(reinterpret_cast<const Impl*>(storage_));
}

NOINLINE FastPimplPlayer::FastPimplPlayer(int id, int vol) {
  static_assert(sizeof(Impl) <= sizeof(storage_), "Impl too large");
  ::new (storage_) Impl(id, vol);
}
NOINLINE FastPimplPlayer::~FastPimplPlayer() { impl()->~Impl(); }
NOINLINE void FastPimplPlayer::seek(int ms) { impl()->posMs = ms; }
NOINLINE int FastPimplPlayer::positionMs() const { return impl()->posMs; }

template <class T>
inline void doNotOptimize(T& val) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : "+r,m"(val) : : "memory");
#else
  volatile T* p = &val;
  (void)*p;
#endif
}

template <class Fn>
double medianNsPerOp(int rounds, int iters, Fn&& fn) {
  std::vector<double> samples;
  samples.reserve(rounds);
  for (int r = 0; r < rounds; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    fn(iters);
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    samples.push_back(ns / iters);
  }
  std::sort(samples.begin(), samples.end());
  return samples[rounds / 2];
}

int main() {
  constexpr int kRounds = 7;
  constexpr int kCtorIters = 200000;
  constexpr int kCallIters = 10000000;
  int64_t sink = 0;

  double directCtor = medianNsPerOp(kRounds, kCtorIters, [&](int n) {
    int64_t acc = 0;
    for (int i = 0; i < n; ++i) {
      DirectPlayer p(i, 80);
      doNotOptimize(p);
      p.seek(i);
      int pos = p.positionMs();
      doNotOptimize(pos);
      acc += pos;
    }
    sink += acc;
  });

  double pimplCtor = medianNsPerOp(kRounds, kCtorIters, [&](int n) {
    int64_t acc = 0;
    for (int i = 0; i < n; ++i) {
      PimplPlayer p(i, 80);
      p.seek(i);
      int pos = p.positionMs();
      doNotOptimize(pos);
      acc += pos;
    }
    sink += acc;
  });

  double fastCtor = medianNsPerOp(kRounds, kCtorIters, [&](int n) {
    int64_t acc = 0;
    for (int i = 0; i < n; ++i) {
      FastPimplPlayer p(i, 80);
      p.seek(i);
      int pos = p.positionMs();
      doNotOptimize(pos);
      acc += pos;
    }
    sink += acc;
  });

  DirectPlayer dp(1, 80);
  double directCall = medianNsPerOp(kRounds, kCallIters, [&](int n) {
    int64_t acc = 0;
    for (int i = 0; i < n; ++i) {
      int v = i;
      doNotOptimize(v);
      dp.seek(v);
      int pos = dp.positionMs();
      doNotOptimize(pos);
      acc += pos;
    }
    sink += acc;
  });

  PimplPlayer pp(1, 80);
  double pimplCall = medianNsPerOp(kRounds, kCallIters, [&](int n) {
    int64_t acc = 0;
    for (int i = 0; i < n; ++i) {
      int v = i;
      doNotOptimize(v);
      pp.seek(v);
      int pos = pp.positionMs();
      doNotOptimize(pos);
      acc += pos;
    }
    sink += acc;
  });

  FastPimplPlayer fp(1, 80);
  double fastCall = medianNsPerOp(kRounds, kCallIters, [&](int n) {
    int64_t acc = 0;
    for (int i = 0; i < n; ++i) {
      int v = i;
      doNotOptimize(v);
      fp.seek(v);
      int pos = fp.positionMs();
      doNotOptimize(pos);
      acc += pos;
    }
    sink += acc;
  });

  std::printf("sizeof: Direct=%zu B, Pimpl=%zu B, FastPimpl=%zu B\n",
              sizeof(DirectPlayer), sizeof(PimplPlayer), sizeof(FastPimplPlayer));
  std::printf("构造+析构（%d 次, 7 轮中位数）:\n", kCtorIters);
  std::printf("  DirectPlayer   : %6.2f ns/op\n", directCtor);
  std::printf("  PimplPlayer    : %6.2f ns/op\n", pimplCtor);
  std::printf("  FastPimplPlayer: %6.2f ns/op\n", fastCtor);
  std::printf("高频调用 seek+pos（%d 次, 7 轮中位数）:\n", kCallIters);
  std::printf("  DirectPlayer   : %6.2f ns/op\n", directCall);
  std::printf("  PimplPlayer    : %6.2f ns/op\n", pimplCall);
  std::printf("  FastPimplPlayer: %6.2f ns/op\n", fastCall);
  std::printf("(sink=%lld)\n", static_cast<long long>(sink));
}
