// 《访问者模式》：操作写成类外的函数，用 dynamic_cast 判断类型；后来加了 Underline，没人改这些函数
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node { virtual ~Node() = default; };      // 元素类里什么操作都没有
using Children = std::vector<std::unique_ptr<Node>>;

struct Text : Node { std::string s; explicit Text(std::string t) : s(std::move(t)) {} };
struct Span : Node { Children kids; };
struct Bold : Span {};
struct Italic : Span {};
struct Color : Span { std::uint32_t rgb; explicit Color(std::uint32_t c) : rgb(c) {} };
struct Br : Node {};
struct Underline : Span {};                      // 后来加的

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

int chars(const Node& n) {
  if (auto t = dynamic_cast<const Text*>(&n)) return utf8Len(t->s);
  if (auto s = dynamic_cast<const Span*>(&n)) { int k = 0; for (auto& c : s->kids) k += chars(*c); return k; }
  return 0;
}

std::string ass(const Node& n) {
  auto kids = [](const Span& s) { std::string r; for (auto& c : s.kids) r += ass(*c); return r; };
  if (auto t = dynamic_cast<const Text*>(&n)) return t->s;
  if (auto b = dynamic_cast<const Bold*>(&n)) return "{\\b1}" + kids(*b) + "{\\b0}";
  if (auto i = dynamic_cast<const Italic*>(&n)) return "{\\i1}" + kids(*i) + "{\\i0}";
  if (auto c = dynamic_cast<const Color*>(&n)) {
    char tag[32];
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c->rgb & 0xFF, (c->rgb >> 8) & 0xFF, c->rgb >> 16);
    return tag + kids(*c) + "{\\c}";
  }
  if (dynamic_cast<const Br*>(&n)) return "\\N";
  return "";                                     // 不认识的类型：什么也不输出
}

// ---- 以下是测试
std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

int main() {
  Children cue;     // Hello <b>big</b> <i><font color=红>world</font></i> ↵ <u>再见</u>
  cue.push_back(text("Hello "));
  cue.push_back(span(Bold{}, text("big")));
  cue.push_back(text(" "));
  cue.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(span(Underline{}, text("再见")));
  int n = 0;
  std::string out;
  for (auto& x : cue) { n += chars(*x); out += ass(*x); }
  std::printf("字数：%d\nASS：%s\n", n, out.c_str());
}
