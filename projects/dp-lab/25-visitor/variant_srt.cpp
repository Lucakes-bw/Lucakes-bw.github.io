// 《访问者模式》：给 variant 版加一种操作「导出 SRT」：只新写一个函数对象
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

struct Node;
using Kids = std::vector<Node>;     // vector 允许元素类型此时还不完整（C++17）

struct Text { std::string s; };
struct Bold { Kids kids; };
struct Italic { Kids kids; };
struct Color { std::uint32_t rgb; Kids kids; };
struct Br {};

struct Node { std::variant<Text, Bold, Italic, Color, Br> v; };

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

// ---- 操作：std::visit 按 v 里实际存的类型调用对应的 operator()，可以直接返回结果
struct CharCounter {
  int operator()(const Text& t) const { return utf8Len(t.s); }
  int operator()(const Bold& b) const { return sum(b.kids); }
  int operator()(const Italic& i) const { return sum(i.kids); }
  int operator()(const Color& c) const { return sum(c.kids); }
  int operator()(const Br&) const { return 0; }
  int sum(const Kids& ks) const { int n = 0; for (auto& k : ks) n += std::visit(*this, k.v); return n; }
};

struct PlainExporter {
  std::string operator()(const Text& t) const { return t.s; }
  std::string operator()(const Bold& b) const { return join(b.kids); }
  std::string operator()(const Italic& i) const { return join(i.kids); }
  std::string operator()(const Color& c) const { return join(c.kids); }
  std::string operator()(const Br&) const { return "\n"; }
  std::string join(const Kids& ks) const { std::string s; for (auto& k : ks) s += std::visit(*this, k.v); return s; }
};

struct AssExporter {
  std::string operator()(const Text& t) const { return t.s; }
  std::string operator()(const Bold& b) const { return "{\\b1}" + join(b.kids) + "{\\b0}"; }
  std::string operator()(const Italic& i) const { return "{\\i1}" + join(i.kids) + "{\\i0}"; }
  std::string operator()(const Color& c) const {
    char tag[32];                     // ASS 的颜色是 &HBBGGRR&
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c.rgb & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb >> 16);
    return tag + join(c.kids) + "{\\c}";
  }
  std::string operator()(const Br&) const { return "\\N"; }
  std::string join(const Kids& ks) const { std::string s; for (auto& k : ks) s += std::visit(*this, k.v); return s; }
};

struct SrtExporter {                // 新操作
  std::string operator()(const Text& t) const { return t.s; }
  std::string operator()(const Bold& b) const { return "<b>" + join(b.kids) + "</b>"; }
  std::string operator()(const Italic& i) const { return "<i>" + join(i.kids) + "</i>"; }
  std::string operator()(const Color& c) const {
    char tag[32];
    std::snprintf(tag, sizeof tag, "<font color=\"#%06X\">", c.rgb);
    return tag + join(c.kids) + "</font>";
  }
  std::string operator()(const Br&) const { return "\n"; }
  std::string join(const Kids& ks) const { std::string s; for (auto& k : ks) s += std::visit(*this, k.v); return s; }
};

// ---- 以下是测试
Kids sample() {   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  return {
      {Text{"Hello "}},
      {Bold{{{Text{"big"}}}}},
      {Text{" "}},
      {Italic{{{Color{0xFF0000, {{Text{"world"}}}}}}}},
      {Br{}},
      {Text{"再见"}},
  };
}

int main() {
  Kids cue = sample();
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", CharCounter{}.sum(cue), PlainExporter{}.join(cue).c_str(),
              AssExporter{}.join(cue).c_str());
  std::printf("SRT：%s\n", SrtExporter{}.join(cue).c_str());
  std::printf("sizeof(Node) = %zu，存了哪种元素：v.index() = %zu\n", sizeof(Node), cue[1].v.index());
}
