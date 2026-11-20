// 《解释器模式》：现代 C++——std::variant 表示 AST + std::visit 同时支持求值与规范打印
#include <cstdio>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct Clip { std::string name; int width, fps, dur; std::string codec; bool hdr; };

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Hdr {};
struct IntCmp   { std::string field, op; int val; };
struct CodecCmp { std::string op, val; };
struct Not      { ExprPtr sub; };
struct And      { ExprPtr l, r; };
struct Or       { ExprPtr l, r; };
struct Expr { std::variant<Hdr, IntCmp, CodecCmp, Not, And, Or> v; };

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

bool eval(const Expr& e, const Clip& c) {
  return std::visit(overloaded{
      [&](const Hdr&) { return c.hdr; },
      [&](const IntCmp& x) {
        int lhs = (x.field == "width") ? c.width : (x.field == "fps") ? c.fps : c.dur;
        if (x.op == "==") return lhs == x.val; if (x.op == "!=") return lhs != x.val;
        if (x.op == "<")  return lhs <  x.val; if (x.op == "<=") return lhs <= x.val;
        if (x.op == ">")  return lhs >  x.val; return lhs >= x.val;
      },
      [&](const CodecCmp& x) { return x.op == "==" ? c.codec == x.val : c.codec != x.val; },
      [&](const Not& x) { return !eval(*x.sub, c); },
      [&](const And& x) { return eval(*x.l, c) && eval(*x.r, c); },
      [&](const Or&  x) { return eval(*x.l, c) || eval(*x.r, c); },
  }, e.v);
}

std::string format(const Expr& e) {
  return std::visit(overloaded{
      [](const Hdr&) { return std::string("hdr"); },
      [](const IntCmp& x) { return x.field + " " + x.op + " " + std::to_string(x.val); },
      [](const CodecCmp& x) { return "codec " + x.op + " \"" + x.val + "\""; },
      [](const Not& x) { return "(not " + format(*x.sub) + ")"; },
      [](const And& x) { return "(" + format(*x.l) + " and " + format(*x.r) + ")"; },
      [](const Or&  x) { return "(" + format(*x.l) + " or "  + format(*x.r) + ")"; },
  }, e.v);
}

template <class T> ExprPtr mk(T x) { return std::make_unique<Expr>(Expr{std::move(x)}); }

int main() {
  std::vector<Clip> clips = {
      {"intro_4k.mov",   3840, 60,  18, "hevc", true},
      {"interview.mp4",  1920, 30, 120, "h264", false},
      {"broll_slow.mp4", 1920, 60,  12, "h264", false},
      {"drone_hdr.mkv",  2560, 30,  25, "hevc", true},
      {"proxy_720.mp4",  1280, 24,   9, "h264", false},
  };
  auto ast = mk(Or{
      mk(And{mk(IntCmp{"width", ">=", 1920}), mk(IntCmp{"fps", ">=", 60})}),
      mk(CodecCmp{"==", "hevc"}),
  });
  std::printf("规范表达式：%s\n命中素材：", format(*ast).c_str());
  for (const auto& c : clips) if (eval(*ast, c)) std::printf(" %s", c.name.c_str());
  std::printf("\n");
}
