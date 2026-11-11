// 《访问者模式》：std::visit 同时分派两个 variant：相邻的两个元素能不能合并，取决于两边各是什么类型
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct Node;
using Kids = std::vector<Node>;
struct Text { std::string s; };
struct Bold { Kids kids; };
struct Italic { Kids kids; };
struct Color { std::uint32_t rgb; Kids kids; };
struct Br {};
struct Node { std::variant<Text, Bold, Italic, Color, Br> v; };

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;   // C++17 要写推导指引，C++20 可以省掉

Kids concat(Kids a, const Kids& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

// 5 种 × 5 种 = 25 种组合，这里只写了 5 个 lambda：4 种能合并的，再加一个兜底
std::optional<Node> merge(const Node& a, const Node& b) {
  return std::visit(overloaded{
      [](const Text& x, const Text& y) -> std::optional<Node> { return Node{Text{x.s + y.s}}; },
      [](const Bold& x, const Bold& y) -> std::optional<Node> { return Node{Bold{concat(x.kids, y.kids)}}; },
      [](const Italic& x, const Italic& y) -> std::optional<Node> { return Node{Italic{concat(x.kids, y.kids)}}; },
      [](const Color& x, const Color& y) -> std::optional<Node> {
        if (x.rgb != y.rgb) return std::nullopt;                  // 颜色不同不能合并
        return Node{Color{x.rgb, concat(x.kids, y.kids)}};
      },
      [](const auto&, const auto&) -> std::optional<Node> { return std::nullopt; },
  }, a.v, b.v);
}

Kids normalize(const Kids& in);
void normalizeKids(Node& n) {        // 带子节点的元素：子节点也要整理
  std::visit(overloaded{[](Text&) {}, [](Br&) {}, [](auto& s) { s.kids = normalize(s.kids); }}, n.v);
}

Kids normalize(const Kids& in) {     // 从左到右，能和前一个合并就合并
  Kids out;
  for (const Node& n : in) {
    if (!out.empty())
      if (auto r = merge(out.back(), n)) {
        out.back() = std::move(*r);
        normalizeKids(out.back());     // 两个 Bold 合并后，里面的两段 Text 也能合并
        continue;
      }
    out.push_back(n);
    normalizeKids(out.back());
  }
  return out;
}

std::string show(const Kids& ks) {   // 用 [b ...] 这样的记号把树打印出来
  std::string s;
  for (auto& k : ks)
    s += std::visit(overloaded{
        [](const Text& t) { return "\"" + t.s + "\""; },
        [](const Bold& b) { return "[b " + show(b.kids) + "]"; },
        [](const Italic& i) { return "[i " + show(i.kids) + "]"; },
        [](const Color& c) { char h[16]; std::snprintf(h, sizeof h, "#%06X", c.rgb); return "[" + std::string(h) + " " + show(c.kids) + "]"; },
        [](const Br&) { return std::string("↵"); },
    }, k.v);
  return s;
}

int count(const Kids& ks) {
  int n = 0;
  for (auto& k : ks) {
    ++n;
    std::visit(overloaded{[](const Text&) {}, [](const Br&) {}, [&](const auto& s) { n += count(s.kids); }}, k.v);
  }
  return n;
}

int main() {
  // 编辑器里逐字修改后，字幕被切成了很多碎片
  Kids cue = {
      {Text{"Hel"}}, {Text{"lo "}},
      {Bold{{{Text{"bi"}}}}}, {Bold{{{Text{"g"}}}}},
      {Text{" "}},
      {Color{0xFF0000, {{Text{"wor"}}}}}, {Color{0xFF0000, {{Text{"ld"}}}}}, {Color{0x0000FF, {{Text{"!"}}}}},
  };
  Kids n = normalize(cue);
  std::printf("整理前 %2d 个节点：%s\n整理后 %2d 个节点：%s\n", count(cue), show(cue).c_str(), count(n), show(n).c_str());
}
