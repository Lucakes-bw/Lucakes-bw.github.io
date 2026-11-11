// 《责任链模式》：四种写法把同一串请求交给 8 个处理者，平均每个请求要多少纳秒（请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

struct Req { int code; long out; };
template <int I> bool rule(Req& r) {        // 第 I 个处理者只管 code == I 的请求
  if (r.code != I) return false;
  r.out += I * 3 + 1;
  return true;
}
constexpr int kN = 8;

// ---- 1. GoF：虚函数 + next_ 指针
struct Linked {
  virtual ~Linked() = default;
  Linked* next = nullptr;
  virtual bool handle(Req& r) = 0;
};
template <int I> struct LinkedH : Linked {
  bool handle(Req& r) override { return rule<I>(r) || (next && next->handle(r)); }
};

// ---- 2. 处理者放进 vector，由外面的循环依次调用
struct Base { virtual ~Base() = default; virtual bool handle(Req& r) = 0; };
template <int I> struct VecH : Base { bool handle(Req& r) override { return rule<I>(r); } };

// ---- 3. vector<std::function>
using Fn = std::function<bool(Req&)>;

// ---- 4. 编译期的链：处理者的类型在模板参数里，折叠表达式展开成一串 ||
template <int... I> bool foldChain(Req& r, std::integer_sequence<int, I...>) { return (rule<I>(r) || ...); }

template <class F> double bench(const std::vector<int>& codes, F&& f) {
  std::vector<double> t;
  long sink = 0;
  for (int rep = 0; rep < 7; ++rep) {
    Req r{0, 0};
    auto t0 = std::chrono::steady_clock::now();
    for (int round = 0; round < 20; ++round)
      for (int c : codes) { r.code = c; f(r); }
    auto t1 = std::chrono::steady_clock::now();
    t.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / (20.0 * codes.size()));
    sink += r.out;
  }
  std::sort(t.begin(), t.end());
  if (sink == 42) std::puts("");
  return t[3];
}

int main() {
  std::vector<int> codes(1 << 20);
  unsigned s = 12345;
  for (int& c : codes) { s = s * 1103515245u + 12345u; c = (s >> 16) % kN; }   // 每个处理者各接到 1/8 的请求

  LinkedH<0> l0; LinkedH<1> l1; LinkedH<2> l2; LinkedH<3> l3; LinkedH<4> l4; LinkedH<5> l5; LinkedH<6> l6; LinkedH<7> l7;
  Linked* ls[] = {&l0, &l1, &l2, &l3, &l4, &l5, &l6, &l7};
  for (int i = 0; i + 1 < kN; ++i) ls[i]->next = ls[i + 1];
  Linked* head = ls[0];

  std::vector<std::unique_ptr<Base>> vec;
  vec.push_back(std::make_unique<VecH<0>>()); vec.push_back(std::make_unique<VecH<1>>());
  vec.push_back(std::make_unique<VecH<2>>()); vec.push_back(std::make_unique<VecH<3>>());
  vec.push_back(std::make_unique<VecH<4>>()); vec.push_back(std::make_unique<VecH<5>>());
  vec.push_back(std::make_unique<VecH<6>>()); vec.push_back(std::make_unique<VecH<7>>());

  std::vector<Fn> fns{rule<0>, rule<1>, rule<2>, rule<3>, rule<4>, rule<5>, rule<6>, rule<7>};

  std::printf("8 个处理者，%zu 个请求 × 20 轮，7 次取中位数\n", codes.size());
  std::printf("GoF 虚函数 + next_     %.2f ns\n", bench(codes, [&](Req& r) { head->handle(r); }));
  std::printf("vector<unique_ptr>    %.2f ns\n", bench(codes, [&](Req& r) { for (auto& h : vec) if (h->handle(r)) break; }));
  std::printf("vector<std::function> %.2f ns\n", bench(codes, [&](Req& r) { for (auto& f : fns) if (f(r)) break; }));
  std::printf("编译期折叠表达式       %.2f ns\n", bench(codes, [&](Req& r) { foldChain(r, std::make_integer_sequence<int, kN>{}); }));
}
