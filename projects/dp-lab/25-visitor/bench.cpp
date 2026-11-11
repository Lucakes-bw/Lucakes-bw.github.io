// 《访问者模式》：单分派虚函数、双分派访问者、dynamic_cast 链、std::visit 四种写法遍历同一串元素的开销（请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <variant>
#include <vector>

static long g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ---- 写法 1、2、3：元素在堆上，用基类指针保存
struct Visitor;
struct Node {
  virtual ~Node() = default;
  virtual long chars() const = 0;               // 写法 1：操作是元素的虚函数（单分派）
  virtual void accept(Visitor& v) const = 0;    // 写法 2：访问者（双分派）
};
struct Text; struct Bold; struct Italic; struct Color; struct Br;
struct Visitor {
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Italic&) = 0;
  virtual void visit(const Color&) = 0;
  virtual void visit(const Br&) = 0;
};
struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  long chars() const override { return (long)s.size(); }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Bold : Node {
  long chars() const override { return 1; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Italic : Node {
  long chars() const override { return 2; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Color : Node {
  unsigned rgb = 0xFF0000;
  long chars() const override { return 3; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Br : Node {
  long chars() const override { return 4; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Counter : Visitor {
  long n = 0;
  void visit(const Text& t) override { n += (long)t.s.size(); }
  void visit(const Bold&) override { n += 1; }
  void visit(const Italic&) override { n += 2; }
  void visit(const Color&) override { n += 3; }
  void visit(const Br&) override { n += 4; }
};
long castChars(const Node& x) {                 // 写法 3
  if (auto t = dynamic_cast<const Text*>(&x)) return (long)t->s.size();
  if (dynamic_cast<const Bold*>(&x)) return 1;
  if (dynamic_cast<const Italic*>(&x)) return 2;
  if (dynamic_cast<const Color*>(&x)) return 3;
  if (dynamic_cast<const Br*>(&x)) return 4;
  return 0;
}

// ---- 写法 4：元素是 variant，按值连续存放
struct VText { std::string s; };
struct VBold {}; struct VItalic {}; struct VColor { unsigned rgb = 0xFF0000; }; struct VBr {};
using VNode = std::variant<VText, VBold, VItalic, VColor, VBr>;
struct VCounter {
  long operator()(const VText& t) const { return (long)t.s.size(); }
  long operator()(const VBold&) const { return 1; }
  long operator()(const VItalic&) const { return 2; }
  long operator()(const VColor&) const { return 3; }
  long operator()(const VBr&) const { return 4; }
};

using Clock = std::chrono::steady_clock;
template <class F> double median7(F f, int n, long& sink) {
  std::vector<double> t;
  for (int r = 0; r < 7; ++r) {
    auto t0 = Clock::now();
    sink += f();
    t.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n);
  }
  std::sort(t.begin(), t.end());
  return t[3];
}

struct Result { double t[4]; long heapAllocs, valAllocs; };

Result run(const std::vector<int>& kinds, long& sink) {
  const int N = (int)kinds.size(), PASSES = 10;
  long a0 = g_allocs;
  std::vector<std::unique_ptr<Node>> heap;
  heap.reserve(N);
  for (int k : kinds) {
    switch (k) {
      case 0: heap.push_back(std::make_unique<Text>("word")); break;
      case 1: heap.push_back(std::make_unique<Bold>()); break;
      case 2: heap.push_back(std::make_unique<Italic>()); break;
      case 3: heap.push_back(std::make_unique<Color>()); break;
      default: heap.push_back(std::make_unique<Br>()); break;
    }
  }
  Result r;
  r.heapAllocs = g_allocs - a0;
  a0 = g_allocs;
  std::vector<VNode> vals;
  vals.reserve(N);
  for (int k : kinds) {
    switch (k) {
      case 0: vals.push_back(VText{"word"}); break;
      case 1: vals.push_back(VBold{}); break;
      case 2: vals.push_back(VItalic{}); break;
      case 3: vals.push_back(VColor{}); break;
      default: vals.push_back(VBr{}); break;
    }
  }
  r.valAllocs = g_allocs - a0;
  r.t[0] = median7([&] { long s = 0; for (int p = 0; p < PASSES; ++p) for (auto& x : heap) s += x->chars(); return s; }, N * PASSES, sink);
  r.t[1] = median7([&] { Counter c; for (int p = 0; p < PASSES; ++p) for (auto& x : heap) x->accept(c); return c.n; }, N * PASSES, sink);
  r.t[2] = median7([&] { long s = 0; for (int p = 0; p < PASSES; ++p) for (auto& x : heap) s += castChars(*x); return s; }, N * PASSES, sink);
  r.t[3] = median7([&] { long s = 0; for (int p = 0; p < PASSES; ++p) for (auto& x : vals) s += std::visit(VCounter{}, x); return s; }, N * PASSES, sink);
  return r;
}

int main() {
  const int N = 1 << 20;
  std::mt19937 rng(42);
  std::vector<int> kinds(N);
  for (int& k : kinds) k = (int)(rng() % 5);    // 5 种元素随机排列
  long sink = 0;
  Result rnd = run(kinds, sink);
  std::sort(kinds.begin(), kinds.end());         // 同样的元素，按类型排好序：分支好预测
  Result srt = run(kinds, sink);

  const char* names[4] = {"单分派虚函数 x->chars()", "双分派访问者 x->accept(v)", "dynamic_cast 链", "std::visit（值连续存放）"};
  std::printf("%d 个元素 × 10 遍，7 次取中位数，每个元素平均（随机排列 / 按类型排序）：\n", N);
  for (int i = 0; i < 4; ++i) std::printf("  %5.2f ns / %5.2f ns  %s\n", rnd.t[i], srt.t[i], names[i]);
  std::printf("建树时的堆分配：基类指针 %ld 次，variant %ld 次\n", rnd.heapAllocs, rnd.valAllocs);
  std::printf("sizeof：Text %zu、Br %zu（再加 unique_ptr %zu），variant %zu\n", sizeof(Text), sizeof(Br),
              sizeof(std::unique_ptr<Node>), sizeof(VNode));
  std::printf("（校验和 %ld）\n", sink);
}
