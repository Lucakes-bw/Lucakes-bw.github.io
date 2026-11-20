// 《解释器模式》：性能压测——10 万条视频元数据求值，对比硬编码、虚函数 AST、std::variant AST、每次重解析（-O2，7 轮中位数）
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

struct Clip {
  int width, fps, dur;
  std::string codec;
  bool hdr;
};

enum class Field { Width, Fps, Dur };
enum class Op { Eq, Ne, Lt, Le, Gt, Ge };

static Field parseField(const std::string& s) {
  return s == "width" ? Field::Width : s == "fps" ? Field::Fps : Field::Dur;
}
static Op parseOp(const std::string& s) {
  if (s == "==") return Op::Eq; if (s == "!=") return Op::Ne;
  if (s == "<")  return Op::Lt; if (s == "<=") return Op::Le;
  if (s == ">")  return Op::Gt; return Op::Ge;
}
static inline bool cmpInt(Field f, Op op, int val, const Clip& c) {
  int lhs = (f == Field::Width) ? c.width : (f == Field::Fps) ? c.fps : c.dur;
  switch (op) {
    case Op::Eq: return lhs == val; case Op::Ne: return lhs != val;
    case Op::Lt: return lhs <  val; case Op::Le: return lhs <= val;
    case Op::Gt: return lhs >  val; case Op::Ge: return lhs >= val;
  }
  return false;
}

// 1. 经典虚函数 AST
namespace virt {
struct Expr { virtual ~Expr() = default; virtual bool eval(const Clip&) const = 0; };
using Ptr = std::unique_ptr<Expr>;
struct Hdr : Expr { bool eval(const Clip& c) const override { return c.hdr; } };
struct IntCmp : Expr {
  Field f; Op op; int val;
  IntCmp(Field f, Op op, int v) : f(f), op(op), val(v) {}
  bool eval(const Clip& c) const override { return cmpInt(f, op, val, c); }
};
struct CodecCmp : Expr {
  bool eq; std::string val;
  CodecCmp(bool eq, std::string v) : eq(eq), val(std::move(v)) {}
  bool eval(const Clip& c) const override { return eq ? c.codec == val : c.codec != val; }
};
struct Not : Expr {
  Ptr s; explicit Not(Ptr s) : s(std::move(s)) {}
  bool eval(const Clip& c) const override { return !s->eval(c); }
};
struct And : Expr {
  Ptr l, r; And(Ptr l, Ptr r) : l(std::move(l)), r(std::move(r)) {}
  bool eval(const Clip& c) const override { return l->eval(c) && r->eval(c); }
};
struct Or : Expr {
  Ptr l, r; Or(Ptr l, Ptr r) : l(std::move(l)), r(std::move(r)) {}
  bool eval(const Clip& c) const override { return l->eval(c) || r->eval(c); }
};

struct Parser {
  std::vector<std::string> tok; size_t pos = 0;
  explicit Parser(const std::string& src) {
    for (size_t i = 0; i < src.size(); ) {
      if (std::isspace((unsigned char)src[i])) { ++i; continue; }
      if (src[i] == '(' || src[i] == ')') { tok.emplace_back(1, src[i++]); continue; }
      if (src[i] == '"') { size_t j = src.find('"', ++i); tok.push_back(src.substr(i, j - i)); i = j + 1; continue; }
      if (std::strchr("=!<>", src[i])) {
        size_t j = i + 1; if (j < src.size() && src[j] == '=') ++j;
        tok.push_back(src.substr(i, j - i)); i = j; continue;
      }
      size_t j = i; while (j < src.size() && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
      tok.push_back(src.substr(i, j - i)); i = j;
    }
  }
  bool match(const char* s) { return pos < tok.size() && tok[pos] == s ? (++pos, true) : false; }
  Ptr parseOr()  { auto e = parseAnd(); while (match("or"))  e = std::make_unique<Or>(std::move(e), parseAnd()); return e; }
  Ptr parseAnd() { auto e = parseNot(); while (match("and")) e = std::make_unique<And>(std::move(e), parseNot()); return e; }
  Ptr parseNot() { return match("not") ? std::make_unique<Not>(parseNot()) : parseAtom(); }
  Ptr parseAtom() {
    if (match("(")) { auto e = parseOr(); if (!match(")")) throw std::runtime_error("missing )"); return e; }
    if (match("hdr")) return std::make_unique<Hdr>();
    std::string f = tok.at(pos++), op = tok.at(pos++), v = tok.at(pos++);
    if (f == "codec") return std::make_unique<CodecCmp>(op == "==", std::move(v));
    return std::make_unique<IntCmp>(parseField(f), parseOp(op), std::stoi(v));
  }
};
}  // namespace virt

// 2. std::variant AST（扁平数组索引节点，值语义零逐节点堆分配）
namespace var {
struct Hdr {};
struct IntCmp { Field f; Op op; int val; };
struct CodecCmp { bool eq; std::string val; };
struct Not { int sub; };
struct And { int l, r; };
struct Or  { int l, r; };
using Node = std::variant<Hdr, IntCmp, CodecCmp, Not, And, Or>;

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

struct Program {
  std::vector<Node> nodes;
  int root = 0;
  bool evalNode(int id, const Clip& c) const {
    return std::visit(overloaded{
        [&](const Hdr&) { return c.hdr; },
        [&](const IntCmp& x) { return cmpInt(x.f, x.op, x.val, c); },
        [&](const CodecCmp& x) { return x.eq ? c.codec == x.val : c.codec != x.val; },
        [&](const Not& x) { return !evalNode(x.sub, c); },
        [&](const And& x) { return evalNode(x.l, c) && evalNode(x.r, c); },
        [&](const Or&  x) { return evalNode(x.l, c) || evalNode(x.r, c); },
    }, nodes[id]);
  }
  bool eval(const Clip& c) const { return evalNode(root, c); }
};
}  // namespace var

template <class Fn>
double benchMedianMs(Fn&& fn, int rounds, int& outCount) {
  std::vector<double> ms;
  ms.reserve(rounds);
  for (int r = 0; r < rounds; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    outCount = fn();
    auto t1 = std::chrono::steady_clock::now();
    ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  std::sort(ms.begin(), ms.end());
  return ms[rounds / 2];
}

int main() {
  constexpr int N = 100000;
  constexpr int ROUNDS = 7;
  std::mt19937 rng(20261120);
  const int widths[] = {1280, 1920, 2560, 3840};
  const int fpss[]   = {24, 30, 60, 120};
  const char* codecs[] = {"h264", "hevc", "av1", "prores"};

  std::vector<Clip> clips;
  clips.reserve(N);
  for (int i = 0; i < N; ++i) {
    clips.push_back({
        widths[rng() % 4],
        fpss[rng() % 4],
        int(5 + rng() % 180),
        codecs[rng() % 4],
        (rng() % 3) == 0,
    });
  }

  const std::string query = "(width >= 1920 and fps >= 60) or codec == \"hevc\"";
  auto virtAst = virt::Parser(query).parseOr();

  var::Program varProg;
  varProg.nodes = {
      var::IntCmp{Field::Width, Op::Ge, 1920},  // 0
      var::IntCmp{Field::Fps,   Op::Ge, 60},    // 1
      var::And{0, 1},                           // 2
      var::CodecCmp{true, "hevc"},              // 3
      var::Or{2, 3},                            // 4
  };
  varProg.root = 4;

  // 预热
  volatile int sink = 0;
  for (int i = 0; i < 1000; ++i) sink += virtAst->eval(clips[i]);

  int c1 = 0, c2 = 0, c3 = 0, c4 = 0;
  double tHard = benchMedianMs([&]() {
    int cnt = 0;
    for (const auto& c : clips)
      if ((c.width >= 1920 && c.fps >= 60) || c.codec == "hevc") ++cnt;
    return cnt;
  }, ROUNDS, c1);

  double tVirt = benchMedianMs([&]() {
    int cnt = 0;
    for (const auto& c : clips) if (virtAst->eval(c)) ++cnt;
    return cnt;
  }, ROUNDS, c2);

  double tVar = benchMedianMs([&]() {
    int cnt = 0;
    for (const auto& c : clips) if (varProg.eval(c)) ++cnt;
    return cnt;
  }, ROUNDS, c3);

  double tReparse = benchMedianMs([&]() {
    int cnt = 0;
    for (const auto& c : clips) {
      auto ast = virt::Parser(query).parseOr();
      if (ast->eval(c)) ++cnt;
    }
    return cnt;
  }, ROUNDS, c4);

  if (c1 != c2 || c2 != c3 || c3 != c4) {
    std::fprintf(stderr, "结果不一致：%d %d %d %d\n", c1, c2, c3, c4);
    return 1;
  }
  std::printf("样本数：%d，命中数：%d（%d 轮中位数）\n", N, c1, ROUNDS);
  std::printf("手写 C++ 硬编码      : %7.3f ms  (%6.1f ns/条)\n", tHard,    tHard    * 1e6 / N);
  std::printf("std::variant AST     : %7.3f ms  (%6.1f ns/条)\n", tVar,     tVar     * 1e6 / N);
  std::printf("经典虚函数 AST       : %7.3f ms  (%6.1f ns/条)\n", tVirt,    tVirt    * 1e6 / N);
  std::printf("每次重解析字符串     : %7.3f ms  (%6.1f ns/条)\n", tReparse, tReparse * 1e6 / N);
  (void)sink;
}
