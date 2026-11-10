// 《装饰器模式》：套 N 层装饰器，每次 read() 多花多少时间（虚函数 / 模板 mixin / std::function 三种写法，请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

constexpr int kCalls = 10'000'000;
volatile long long sink;
volatile long long step = 1;                     // 从 volatile 变量读步长：不让编译器把整个循环算成一个公式

// ---------- 1. 虚函数：每层一个对象，read() 转发给里面一层再加 1 ----------
struct Source { virtual ~Source() = default; virtual long long read() = 0; };
struct Counter : Source { long long i = 0; long long read() override { return i += step; } };
struct AddOne : Source {
    explicit AddOne(std::unique_ptr<Source> in) : in_(std::move(in)) {}
    long long read() override { return in_->read() + 1; }
    std::unique_ptr<Source> in_;
};
[[gnu::noinline]] std::unique_ptr<Source> wrap(int layers) {     // 层数在运行时才知道，编译器没法去掉虚调用
    std::unique_ptr<Source> s = std::make_unique<Counter>();
    for (int i = 0; i < layers; ++i) s = std::make_unique<AddOne>(std::move(s));
    return s;
}

// ---------- 2. 模板 mixin：AddOneT<AddOneT<...<CounterT>>>，层数是类型的一部分 ----------
struct CounterT { long long i = 0; long long read() { return i += step; } };
template <class Base> struct AddOneT : Base { long long read() { return Base::read() + 1; } };
template <int N> struct Nest { using type = AddOneT<typename Nest<N - 1>::type>; };
template <> struct Nest<0> { using type = CounterT; };

// ---------- 3. std::function：每层一个 lambda，捕获里面一层 ----------
[[gnu::noinline]] std::function<long long()> wrapFn(int layers) {
    auto c = std::make_shared<long long>(0);
    std::function<long long()> f = [c] { return *c += step; };
    for (int i = 0; i < layers; ++i) f = [g = std::move(f)] { return g() + 1; };
    return f;
}

template <class F> double nsPerCall(F&& read) {  // 重复 7 次取中位数
    std::vector<double> t;
    for (int r = 0; r < 7; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        long long acc = 0;
        for (int i = 0; i < kCalls; ++i) acc += read();
        sink = acc;
        auto t1 = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kCalls);
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

template <int N> double mixin() { typename Nest<N>::type s; return nsPerCall([&] { return s.read(); }); }

int main() {
    std::printf("层数   虚函数    模板mixin  std::function  (ns/次)\n");
    const int layers[] = {0, 1, 2, 4, 8, 16};
    const double mix[] = {mixin<0>(), mixin<1>(), mixin<2>(), mixin<4>(), mixin<8>(), mixin<16>()};
    for (int k = 0; k < 6; ++k) {
        auto v = wrap(layers[k]);
        auto f = wrapFn(layers[k]);
        double a = nsPerCall([&] { return v->read(); });
        double c = nsPerCall([&] { return f(); });
        std::printf("%4d %8.2f %10.2f %12.2f\n", layers[k], a, mix[k], c);
    }
    // 参照：一次真正的「读包」至少要复制数据。复制一个 1.7 KB 的包要多久
    std::vector<unsigned char> from(1700, 7), to;
    std::printf("参照：复制 1.7 KB 的包 %.2f ns/次\n", nsPerCall([&] { to.assign(from.begin(), from.end()); return (long long)to[0]; }));
}
