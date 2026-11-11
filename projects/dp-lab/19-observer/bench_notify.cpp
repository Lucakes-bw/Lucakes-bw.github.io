// 《观察者模式》：通知开销实测——8 个观察者，比较直接调用、虚函数接口、std::function、Signal、weak_ptr 几种写法每次通知的耗时
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

constexpr int kObs = 8;
constexpr int kN = 2'000'000;          // 每轮通知次数
constexpr int kReps = 7;               // 重复 7 轮取中位数

struct Observer {                      // 虚函数接口
  virtual ~Observer() = default;
  virtual void onProgress(double pos) = 0;
  double sum = 0;
};
// 4 种不同的观察者，避免编译器把虚调用猜成同一个类型
struct A : Observer { void onProgress(double p) override { sum += p; } };
struct B : Observer { void onProgress(double p) override { sum += p * 2; } };
struct C : Observer { void onProgress(double p) override { sum += p + 1; } };
struct D : Observer { void onProgress(double p) override { sum -= p; } };

std::vector<std::shared_ptr<Observer>> make_observers() {
  std::vector<std::shared_ptr<Observer>> v;
  for (int i = 0; i < kObs; ++i) {
    switch (i % 4) {
      case 0: v.push_back(std::make_shared<A>()); break;
      case 1: v.push_back(std::make_shared<B>()); break;
      case 2: v.push_back(std::make_shared<C>()); break;
      default: v.push_back(std::make_shared<D>()); break;
    }
  }
  return v;
}

double total(const std::vector<std::shared_ptr<Observer>>& v) {
  double s = 0;
  for (auto& o : v) s += o->sum;
  return s;
}

// 1. 硬编码：直接调用 8 个具体对象的非虚函数（可以内联）
struct Direct {
  A a0, a1; B b0, b1; C c0, c1; D d0, d1;
  void notify(double p) {
    a0.A::onProgress(p); b0.B::onProgress(p); c0.C::onProgress(p); d0.D::onProgress(p);
    a1.A::onProgress(p); b1.B::onProgress(p); c1.C::onProgress(p); d1.D::onProgress(p);
  }
  double sum() const { return a0.sum + a1.sum + b0.sum + b1.sum + c0.sum + c1.sum + d0.sum + d1.sum; }
};

// Signal 的通知循环（和 signal.cpp 一样：下标遍历，每个槽复制一次 shared_ptr）
struct Slot { std::function<void(double)> fn; bool alive = true; };

template <class F>
double median_ns(F&& body) {
  std::vector<double> t;
  body();                                        // 先热身一轮，不计时
  for (int r = 0; r < kReps; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    body();
    auto t1 = std::chrono::steady_clock::now();
    t.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kN);
  }
  std::sort(t.begin(), t.end());
  return t[kReps / 2];
}

int main() {
  double check = 0;

  Direct direct;
  double t_direct = median_ns([&] { for (int i = 0; i < kN; ++i) direct.notify(i); });
  check += direct.sum();

  auto objs = make_observers();
  std::vector<Observer*> raw;
  for (auto& o : objs) raw.push_back(o.get());
  double t_virtual = median_ns([&] {
    for (int i = 0; i < kN; ++i)
      for (auto* o : raw) o->onProgress(i);
  });

  std::vector<std::function<void(double)>> fns;
  for (auto& o : objs) fns.push_back([p = o.get()](double x) { p->onProgress(x); });
  double t_function = median_ns([&] {
    for (int i = 0; i < kN; ++i)
      for (auto& f : fns) f(i);
  });

  std::vector<std::shared_ptr<Slot>> slots;
  for (auto& o : objs) slots.push_back(std::make_shared<Slot>(Slot{[p = o.get()](double x) { p->onProgress(x); }}));
  double t_signal = median_ns([&] {
    for (int i = 0; i < kN; ++i) {
      const std::size_t n = slots.size();
      for (std::size_t k = 0; k < n; ++k) {
        auto s = slots[k];                       // 复制 shared_ptr：原子加一、原子减一
        if (s->alive) s->fn(i);
      }
    }
  });

  std::vector<std::weak_ptr<Observer>> weak(objs.begin(), objs.end());
  double t_weak = median_ns([&] {
    for (int i = 0; i < kN; ++i)
      for (auto& w : weak)
        if (auto o = w.lock()) o->onProgress(i);
  });

  double t_weak_snap = median_ns([&] {
    for (int i = 0; i < kN; ++i) {
      auto snapshot = weak;                      // weak_observer.cpp 的写法：每次通知先复制整个列表
      for (auto& w : snapshot)
        if (auto o = w.lock()) o->onProgress(i);
    }
  });
  check += total(objs);

  std::printf("%d 个观察者，每次通知的耗时（ns，%d 次 × %d 轮取中位数）\n", kObs, kN, kReps);
  std::printf("  硬编码直接调用            %6.2f\n", t_direct);
  std::printf("  虚函数接口 vector<T*>     %6.2f\n", t_virtual);
  std::printf("  std::function             %6.2f\n", t_function);
  std::printf("  Signal（复制 shared_ptr） %6.2f\n", t_signal);
  std::printf("  weak_ptr::lock            %6.2f\n", t_weak);
  std::printf("  weak_ptr + 每次复制快照   %6.2f\n", t_weak_snap);
  std::printf("（校验和 %g）\n", check);
}
