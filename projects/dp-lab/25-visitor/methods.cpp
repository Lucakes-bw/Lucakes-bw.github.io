// 《访问者模式》：把每种操作都写成元素类的虚函数：字幕元素树会统计字数、导出纯文本、导出 ASS
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node {                       // 每种操作一个虚函数
  virtual ~Node() = default;
  virtual int chars() const = 0;
  virtual std::string plain() const = 0;
  virtual std::string ass() const = 0;
};
using Children = std::vector<std::unique_ptr<Node>>;

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  int chars() const override { return utf8Len(s); }
  std::string plain() const override { return s; }
  std::string ass() const override { return s; }
};
struct Span : Node {                // 带子节点的元素：字数和纯文本都是把子节点的拼起来
  Children kids;
  int chars() const override { int n = 0; for (auto& k : kids) n += k->chars(); return n; }
  std::string plain() const override { std::string s; for (auto& k : kids) s += k->plain(); return s; }
  std::string kidsAss() const { std::string s; for (auto& k : kids) s += k->ass(); return s; }
};
struct Bold : Span {
  std::string ass() const override { return "{\\b1}" + kidsAss() + "{\\b0}"; }
};
struct Italic : Span {
  std::string ass() const override { return "{\\i1}" + kidsAss() + "{\\i0}"; }
};
struct Color : Span {
  std::uint32_t rgb;
  explicit Color(std::uint32_t c) : rgb(c) {}
  std::string ass() const override {
    char tag[32];                     // ASS 的颜色是 &HBBGGRR&
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", rgb & 0xFF, (rgb >> 8) & 0xFF, rgb >> 16);
    return tag + kidsAss() + "{\\c}";
  }
};
struct Br : Node {
  int chars() const override { return 0; }
  std::string plain() const override { return "\n"; }
  std::string ass() const override { return "\\N"; }
};

// ---- 以下是测试
struct Cue {                        // 一条字幕
  Children items;
  template <class F> auto join(F f) const { decltype(f(*items[0])) r{}; for (auto& n : items) r += f(*n); return r; }
};

std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

Cue sample() {   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  Cue c;
  c.items.push_back(text("Hello "));
  c.items.push_back(span(Bold{}, text("big")));
  c.items.push_back(text(" "));
  c.items.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  c.items.push_back(std::make_unique<Br>());
  c.items.push_back(text("再见"));
  return c;
}

int main() {
  Cue cue = sample();
  int n = cue.join([](const Node& x) { return x.chars(); });
  std::string plain = cue.join([](const Node& x) { return x.plain(); });
  std::string ass = cue.join([](const Node& x) { return x.ass(); });
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", n, plain.c_str(), ass.c_str());
}
