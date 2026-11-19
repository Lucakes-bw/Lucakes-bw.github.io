// 《中介者模式》：实测网状直接调用、虚函数中介者、枚举事件中介者、std::function 回调中介者的协调开销和同事对象大小
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

// ---- 1. 网状直接调用：每个控件持有另外 3 个控件的指针
struct MB; struct MC; struct MD;
struct MA { int v = 0; MB* b; MC* c; MD* d; __attribute__((noinline)) void fire(int x); };
struct MB { int v = 0; MA* a; MC* c; MD* d; };
struct MC { int v = 0; MA* a; MB* b; MD* d; };
struct MD { int v = 0; MA* a; MB* b; MC* c; void check() { v = (a->v + b->v + c->v) & 1; } };
__attribute__((noinline)) void MA::fire(int x) {
  v = x;
  b->v = x + 1;
  c->v = (x & 1) ? 2 : 3;
  d->check();
}

// ---- 2. 经典虚函数中介者：同事只持有 1 个 Mediator*，通过 sender 指针判断是谁
struct VCol;
struct VMed {
  virtual ~VMed() = default;
  virtual void changed(VCol* s) = 0;
};
struct VCol {
  VMed* m;
  int v = 0;
  explicit VCol(VMed* m) : m(m) {}
  __attribute__((noinline)) void fire(int x) { v = x; m->changed(this); }
};
struct VDialog : VMed {
  VCol a{this}, b{this}, c{this}, d{this};
  void changed(VCol* s) override {
    if (s == &a) {
      b.v = a.v + 1;
      c.v = (a.v & 1) ? 2 : 3;
    }
    d.v = (a.v + b.v + c.v) & 1;
  }
};

// ---- 3. 枚举事件中介者：用事件 ID 代替 Widget*，省掉指针比较或 dynamic_cast
enum class Ev { AChanged, BChanged };
struct EMed {
  virtual ~EMed() = default;
  virtual void onEvent(Ev e) = 0;
};
struct ECol {
  EMed* m;
  int v = 0;
  explicit ECol(EMed* m) : m(m) {}
  __attribute__((noinline)) void fire(int x) { v = x; m->onEvent(Ev::AChanged); }
};
struct EDialog : EMed {
  ECol a{this}, b{this}, c{this}, d{this};
  void onEvent(Ev e) override {
    if (e == Ev::AChanged) {
      b.v = a.v + 1;
      c.v = (a.v & 1) ? 2 : 3;
    }
    d.v = (a.v + b.v + c.v) & 1;
  }
};

// ---- 4. std::function 回调中介者：同事不认识中介者接口
struct FCol {
  int v = 0;
  std::function<void()> onChange;
  __attribute__((noinline)) void fire(int x) { v = x; onChange(); }
};
struct FDialog {
  FCol a, b, c, d;
  FDialog() {
    a.onChange = [this] {
      b.v = a.v + 1;
      c.v = (a.v & 1) ? 2 : 3;
      d.v = (a.v + b.v + c.v) & 1;
    };
  }
};

using Clock = std::chrono::steady_clock;
template <class F>
double median7(F fn, int n, long& sink) {
  std::vector<double> t;
  for (int r = 0; r < 7; ++r) {
    auto t0 = Clock::now();
    sink += fn();
    t.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n);
  }
  std::sort(t.begin(), t.end());
  return t[3];
}

int main() {
  const int N = 1 << 22;
  long sink = 0;

  MA a; MB b; MC c; MD d;
  a.b = &b; a.c = &c; a.d = &d;
  b.a = &a; b.c = &c; b.d = &d;
  c.a = &a; c.b = &b; c.d = &d;
  d.a = &a; d.b = &b; d.c = &c;

  VDialog vd;
  EDialog ed;
  FDialog fd;

  double t1 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { a.fire(i); s += d.v; } return s; }, N, sink);
  double t2 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { vd.a.fire(i); s += vd.d.v; } return s; }, N, sink);
  double t3 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { ed.a.fire(i); s += ed.d.v; } return s; }, N, sink);
  double t4 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { fd.a.fire(i); s += fd.d.v; } return s; }, N, sink);

  std::printf("%d 次控件联动，7 轮取中位数，每次联动平均耗时：\n", N);
  std::printf("  %5.2f ns  网状直接调用         （单个同事 sizeof = %2zu，存 3 个邻居指针）\n", t1, sizeof(MA));
  std::printf("  %5.2f ns  虚函数中介者(Widget*)（单个同事 sizeof = %2zu，只存 1 个中介者指针）\n", t2, sizeof(VCol));
  std::printf("  %5.2f ns  虚函数中介者(枚举事件)（单个同事 sizeof = %2zu，只存 1 个中介者指针）\n", t3, sizeof(ECol));
  std::printf("  %5.2f ns  std::function 回调   （单个同事 sizeof = %2zu，含一个 std::function）\n", t4, sizeof(FCol));
  std::printf("（校验和 %ld）\n", sink);
}
