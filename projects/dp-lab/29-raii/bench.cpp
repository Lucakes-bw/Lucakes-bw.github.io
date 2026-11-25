// 《RAII 与 Scope Guard》：压测——对比裸指针手动释放、手写 RAII Handle、unique_ptr 空删除器与函数指针删除器的 sizeof 及千万次开关耗时
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr int kIters  = 50'000'000;
constexpr int kRounds = 7;

struct RawHandle {
  uint64_t token;
};

// 用静态对象池模拟 C 句柄分配/释放，剥离 malloc 噪声，专注测量 RAII 抽象层本身开销
RawHandle g_pool[4] = {{1}, {2}, {3}, {4}};
uint64_t g_checksum = 0;

inline RawHandle* acquire_handle(int i) noexcept {
  RawHandle* h = &g_pool[i & 3];
  h->token += static_cast<uint64_t>(i);
  return h;
}

inline void release_handle_inline(RawHandle* h) noexcept {
  if (h) g_checksum += h->token;
}

// 模拟来自外部 C 动态库（跨翻译单元无法内联）的释放函数，供函数指针删除器调用
__attribute__((noinline)) void release_handle_capi(RawHandle* h) noexcept {
  if (h) g_checksum += h->token;
}

// 方案 2：手写只移动 RAII Handle
class ManualHandle {
 public:
  explicit ManualHandle(RawHandle* p = nullptr) noexcept : p_(p) {}
  ~ManualHandle() noexcept { release_handle_inline(p_); }
  ManualHandle(const ManualHandle&) = delete;
  ManualHandle& operator=(const ManualHandle&) = delete;
  ManualHandle(ManualHandle&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
  ManualHandle& operator=(ManualHandle&& o) noexcept {
    if (this != &o) { release_handle_inline(p_); p_ = std::exchange(o.p_, nullptr); }
    return *this;
  }
  RawHandle* get() const noexcept { return p_; }
 private:
  RawHandle* p_;
};

// 方案 3：std::unique_ptr + 空函数对象删除器（EBO 零空间开销 + 可内联）
struct EmptyDeleter {
  void operator()(RawHandle* p) const noexcept { release_handle_inline(p); }
};
using UniqueEmptyPtr = std::unique_ptr<RawHandle, EmptyDeleter>;

// 方案 4：std::unique_ptr + 函数指针删除器（多存 8 字节函数指针 + 间接调用）
using UniqueFnPtr = std::unique_ptr<RawHandle, void (*)(RawHandle*)>;

volatile uint64_t g_sink = 0;

double median(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

template <typename Fn>
double runBench(Fn&& fn) {
  // 预热一轮，避免冷启动降频影响首次测量
  g_checksum = 0;
  fn();
  g_sink += g_checksum;

  std::vector<double> samples;
  samples.reserve(kRounds);
  for (int r = 0; r < kRounds; ++r) {
    g_checksum = 0;
    auto t0 = std::chrono::steady_clock::now();
    fn();
    auto t1 = std::chrono::steady_clock::now();
    g_sink += g_checksum;
    samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  return median(samples);
}

}  // namespace

int main() {
  double rawMs = runBench([] {
    for (int i = 0; i < kIters; ++i) {
      RawHandle* h = acquire_handle(i);
      h->token ^= 0x9e3779b97f4a7c15ULL;
      release_handle_inline(h);
    }
  });

  double handleMs = runBench([] {
    for (int i = 0; i < kIters; ++i) {
      ManualHandle h(acquire_handle(i));
      h.get()->token ^= 0x9e3779b97f4a7c15ULL;
    }
  });

  double emptyMs = runBench([] {
    for (int i = 0; i < kIters; ++i) {
      UniqueEmptyPtr h(acquire_handle(i));
      h->token ^= 0x9e3779b97f4a7c15ULL;
    }
  });

  void (*volatile deleterFn)(RawHandle*) = &release_handle_capi;
  double fnPtrMs = runBench([&] {
    auto fn = deleterFn;
    for (int i = 0; i < kIters; ++i) {
      UniqueFnPtr h(acquire_handle(i), fn);
      h->token ^= 0x9e3779b97f4a7c15ULL;
    }
  });

  std::printf("5000 万次开关循环（%d 轮中位数）：\n", kRounds);
  std::printf("  裸指针手动释放                ：sizeof=%2zu B | %6.2f ms (%.2f ns/次)\n",
              sizeof(RawHandle*), rawMs, rawMs * 1e6 / kIters);
  std::printf("  手写 RAII Handle (Rule of 5)  ：sizeof=%2zu B | %6.2f ms (%.2f ns/次)\n",
              sizeof(ManualHandle), handleMs, handleMs * 1e6 / kIters);
  std::printf("  unique_ptr<T, EmptyDeleter>   ：sizeof=%2zu B | %6.2f ms (%.2f ns/次)\n",
              sizeof(UniqueEmptyPtr), emptyMs, emptyMs * 1e6 / kIters);
  std::printf("  unique_ptr<T, void(*)(T*)>    ：sizeof=%2zu B | %6.2f ms (%.2f ns/次)\n",
              sizeof(UniqueFnPtr), fnPtrMs, fnPtrMs * 1e6 / kIters);
}
