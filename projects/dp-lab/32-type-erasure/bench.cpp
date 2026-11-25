// 《类型擦除》：实测对比「传统虚基类 unique_ptr」「Concept/Model」「SBO + VTable」「std::variant」
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// 统计堆分配次数（仅在计时区间打开开关，避免统计容器本身 reserve）
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

constexpr int kCount  = 1000000;  // 100 万个图层对象
constexpr int kRounds = 7;

volatile long long g_sink = 0;

// 三个独立图层类型（均为小对象 <= 24 B，自身不做堆分配）
struct VideoLayer {
  int w, h, fps;
  long long draw() const { return static_cast<long long>(w) * h + fps; }
};

struct SubtitleLayer {
  const char* text;
  int len, size;
  long long draw() const { return static_cast<long long>(len) * size + text[0]; }
};

struct WatermarkLayer {
  int x, y, alpha;
  long long draw() const { return static_cast<long long>(x + y) * alpha; }
};

// 方案 1：传统虚基类 + unique_ptr
struct IDrawable {
  virtual ~IDrawable() = default;
  virtual long long draw() const = 0;
};
struct VVideo final : IDrawable {
  VideoLayer v;
  explicit VVideo(VideoLayer x) : v(x) {}
  long long draw() const override { return v.draw(); }
};
struct VSub final : IDrawable {
  SubtitleLayer s;
  explicit VSub(SubtitleLayer x) : s(x) {}
  long long draw() const override { return s.draw(); }
};
struct VWm final : IDrawable {
  WatermarkLayer w;
  explicit VWm(WatermarkLayer x) : w(x) {}
  long long draw() const override { return w.draw(); }
};

// 方案 2：经典 Concept/Model 类型擦除
class BasicDrawable {
  struct Concept {
    virtual ~Concept() = default;
    virtual long long doDraw() const = 0;
  };
  template <typename T>
  struct Model final : Concept {
    T data_;
    explicit Model(T x) : data_(std::move(x)) {}
    long long doDraw() const override { return data_.draw(); }
  };
  std::unique_ptr<const Concept> self_;
 public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, BasicDrawable>>>
  BasicDrawable(T x) : self_(std::make_unique<Model<std::decay_t<T>>>(std::move(x))) {}
  long long draw() const { return self_->doDraw(); }
};

// 方案 3：32 字节 SBO + 手写 VTable 类型擦除
class SboDrawable {
  static constexpr std::size_t kBufSize  = 32;
  static constexpr std::size_t kBufAlign = alignof(std::max_align_t);
  struct VTable {
    long long (*draw)(const void*);
    void (*move)(void*, void*) noexcept;
    void (*destroy)(void*) noexcept;
  };
  template <typename T>
  static constexpr bool kFits =
      sizeof(T) <= kBufSize && alignof(T) <= kBufAlign && std::is_nothrow_move_constructible_v<T>;

  template <typename T>
  static const VTable* vt() noexcept {
    if constexpr (kFits<T>) {
      static const VTable t{
          [](const void* p) { return static_cast<const T*>(p)->draw(); },
          [](void* s, void* d) noexcept { ::new (d) T(std::move(*static_cast<T*>(s))); },
          [](void* p) noexcept { static_cast<T*>(p)->~T(); }};
      return &t;
    } else {
      static const VTable t{
          [](const void* p) { return (*static_cast<T* const*>(p))->draw(); },
          [](void* s, void* d) noexcept {
            auto*& sp = *static_cast<T**>(s);
            ::new (d) T*(sp);
            sp = nullptr;
          },
          [](void* p) noexcept { delete *static_cast<T**>(p); }};
      return &t;
    }
  }
  alignas(kBufAlign) std::byte buf_[kBufSize]{};
  const VTable* vptr_ = nullptr;
 public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, SboDrawable>>>
  SboDrawable(T x) : vptr_(vt<std::decay_t<T>>()) {
    using U = std::decay_t<T>;
    if constexpr (kFits<U>) ::new (buf_) U(std::move(x));
    else ::new (buf_) U*(new U(std::move(x)));
  }
  SboDrawable(SboDrawable&& o) noexcept : vptr_(o.vptr_) {
    if (vptr_) { vptr_->move(o.buf_, buf_); vptr_->destroy(o.buf_); o.vptr_ = nullptr; }
  }
  ~SboDrawable() { if (vptr_) vptr_->destroy(buf_); }
  long long draw() const { return vptr_->draw(buf_); }
};

// 方案 4：std::variant 封闭集合多态
using VariantDrawable = std::variant<VideoLayer, SubtitleLayer, WatermarkLayer>;

double median(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

struct Result {
  const char* name;
  std::size_t sizeBytes;
  std::size_t allocs;
  double ctorCallNs;
  double hotCallNs;
};

template <typename FnCtor, typename FnHot>
Result runBench(const char* name, std::size_t sz, FnCtor&& fnCtor, FnHot&& fnHot) {
  std::vector<double> tCtor, tHot;
  std::size_t allocs = 0;

  for (int r = 0; r < kRounds; ++r) {
    if (r == 0) { g_allocCount = 0; g_countAlloc = true; }
    auto t0 = std::chrono::steady_clock::now();
    long long acc = fnCtor();
    auto t1 = std::chrono::steady_clock::now();
    if (r == 0) { g_countAlloc = false; allocs = g_allocCount; }
    g_sink += acc;
    tCtor.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kCount);

    auto h0 = std::chrono::steady_clock::now();
    long long acc2 = fnHot();
    auto h1 = std::chrono::steady_clock::now();
    g_sink += acc2;
    tHot.push_back(std::chrono::duration<double, std::nano>(h1 - h0).count() / kCount);
  }
  return {name, sz, allocs, median(tCtor), median(tHot)};
}

}  // namespace

int main() {
  constexpr int kPool = 1024;
  constexpr int kPasses = kCount / kPool;

  // 伪随机类型序列，避免分支预测完全猜中周期为 3 的模式
  std::vector<int> kinds(kCount);
  std::uint32_t rng = 0x12345678u;
  for (int i = 0; i < kCount; ++i) {
    rng = rng * 1664525u + 1013904223u;
    kinds[i] = static_cast<int>((rng >> 16) % 3);
  }

  // 预分配容器缓冲区，只测元素自身构造/派发/析构（不含 vector 扩容）
  std::vector<std::unique_ptr<IDrawable>> vBuf, vPool;
  std::vector<BasicDrawable> bBuf, bPool;
  std::vector<SboDrawable> sBuf, sPool;
  std::vector<VariantDrawable> varBuf, varPool;
  vBuf.reserve(kCount); bBuf.reserve(kCount); sBuf.reserve(kCount); varBuf.reserve(kCount);
  vPool.reserve(kPool); bPool.reserve(kPool); sPool.reserve(kPool); varPool.reserve(kPool);

  for (int i = 0; i < kPool; ++i) {
    int k = kinds[i];
    if (k == 0) {
      VideoLayer x{1920, 1080, 30 + (i & 31)};
      vPool.push_back(std::make_unique<VVideo>(x));
      bPool.emplace_back(x);
      sPool.emplace_back(x);
      varPool.emplace_back(x);
    } else if (k == 1) {
      SubtitleLayer x{"Hello", 5 + (i & 7), 24};
      vPool.push_back(std::make_unique<VSub>(x));
      bPool.emplace_back(x);
      sPool.emplace_back(x);
      varPool.emplace_back(x);
    } else {
      WatermarkLayer x{10 + (i & 15), 20, 60};
      vPool.push_back(std::make_unique<VWm>(x));
      bPool.emplace_back(x);
      sPool.emplace_back(x);
      varPool.emplace_back(x);
    }
  }

  auto r1 = runBench(
      "传统虚基类 unique_ptr", sizeof(std::unique_ptr<IDrawable>),
      [&] {
        vBuf.clear();
        for (int i = 0; i < kCount; ++i) {
          int k = kinds[i];
          if (k == 0) vBuf.push_back(std::make_unique<VVideo>(VideoLayer{1920, 1080, i & 31}));
          else if (k == 1) vBuf.push_back(std::make_unique<VSub>(SubtitleLayer{"Hello", i & 7, 24}));
          else vBuf.push_back(std::make_unique<VWm>(WatermarkLayer{i & 15, 20, 60}));
        }
        long long s = 0;
        for (const auto& item : vBuf) s += item->draw();
        vBuf.clear();
        return s;
      },
      [&] {
        long long s = 0;
        for (int p = 0; p < kPasses; ++p)
          for (const auto& item : vPool) s += item->draw();
        return s;
      });

  auto r2 = runBench(
      "Concept/Model 类型擦除", sizeof(BasicDrawable),
      [&] {
        bBuf.clear();
        for (int i = 0; i < kCount; ++i) {
          int k = kinds[i];
          if (k == 0) bBuf.emplace_back(VideoLayer{1920, 1080, i & 31});
          else if (k == 1) bBuf.emplace_back(SubtitleLayer{"Hello", i & 7, 24});
          else bBuf.emplace_back(WatermarkLayer{i & 15, 20, 60});
        }
        long long s = 0;
        for (const auto& item : bBuf) s += item.draw();
        bBuf.clear();
        return s;
      },
      [&] {
        long long s = 0;
        for (int p = 0; p < kPasses; ++p)
          for (const auto& item : bPool) s += item.draw();
        return s;
      });

  auto r3 = runBench(
      "SBO + VTable 类型擦除", sizeof(SboDrawable),
      [&] {
        sBuf.clear();
        for (int i = 0; i < kCount; ++i) {
          int k = kinds[i];
          if (k == 0) sBuf.emplace_back(VideoLayer{1920, 1080, i & 31});
          else if (k == 1) sBuf.emplace_back(SubtitleLayer{"Hello", i & 7, 24});
          else sBuf.emplace_back(WatermarkLayer{i & 15, 20, 60});
        }
        long long s = 0;
        for (const auto& item : sBuf) s += item.draw();
        sBuf.clear();
        return s;
      },
      [&] {
        long long s = 0;
        for (int p = 0; p < kPasses; ++p)
          for (const auto& item : sPool) s += item.draw();
        return s;
      });

  auto r4 = runBench(
      "std::variant 封闭集合", sizeof(VariantDrawable),
      [&] {
        varBuf.clear();
        for (int i = 0; i < kCount; ++i) {
          int k = kinds[i];
          if (k == 0) varBuf.emplace_back(VideoLayer{1920, 1080, i & 31});
          else if (k == 1) varBuf.emplace_back(SubtitleLayer{"Hello", i & 7, 24});
          else varBuf.emplace_back(WatermarkLayer{i & 15, 20, 60});
        }
        long long s = 0;
        for (const auto& item : varBuf)
          s += std::visit([](const auto& x) { return x.draw(); }, item);
        varBuf.clear();
        return s;
      },
      [&] {
        long long s = 0;
        for (int p = 0; p < kPasses; ++p)
          for (const auto& item : varPool)
            s += std::visit([](const auto& x) { return x.draw(); }, item);
        return s;
      });

  std::printf("100 万个图层：构造+遍历调用+析构 vs 容器热循环单次调用（%d 轮中位数）：\n", kRounds);
  for (const auto& r : {r1, r2, r3, r4}) {
    std::printf("  %-24s | sizeof=%2zu B | 堆分配=%7zu | 构造+调用=%5.2f ns | 热调用=%4.2f ns\n",
                r.name, r.sizeBytes, r.allocs, r.ctorCallNs, r.hotCallNs);
  }
}
