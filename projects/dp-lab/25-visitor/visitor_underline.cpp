// 《访问者模式》：给访问者版加一种元素「下划线」：访问者接口和每个访问者都要改
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Text; struct Bold; struct Italic; struct Color; struct Br; struct Underline;

struct Visitor {                    // 每种元素一个 visit()
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Italic&) = 0;
  virtual void visit(const Color&) = 0;
  virtual void visit(const Br&) = 0;
  virtual void visit(const Underline&) = 0;
};

struct Node {
  virtual ~Node() = default;
  virtual void accept(Visitor& v) const = 0;
};
using Children = std::vector<std::unique_ptr<Node>>;

struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Span : Node {                // 带子节点的元素
  Children kids;
  void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
};
struct Bold : Span {
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Italic : Span {
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Color : Span {
  std::uint32_t rgb;
  explicit Color(std::uint32_t c) : rgb(c) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Br : Node {
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Underline : Span {
  void accept(Visitor& v) const override { v.visit(*this); }
};

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

// ---- 操作：每种一个访问者类，元素类一行都不用改
struct CharCounter : Visitor {      // 统计字数（换行不算）
  int n = 0;
  void visit(const Text& t) override { n += utf8Len(t.s); }
  void visit(const Bold& b) override { b.visitKids(*this); }
  void visit(const Italic& i) override { i.visitKids(*this); }
  void visit(const Color& c) override { c.visitKids(*this); }
  void visit(const Br&) override {}
  void visit(const Underline& u) override { u.visitKids(*this); }
};

struct PlainExporter : Visitor {    // 去掉所有样式，只留文字
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { b.visitKids(*this); }
  void visit(const Italic& i) override { i.visitKids(*this); }
  void visit(const Color& c) override { c.visitKids(*this); }
  void visit(const Br&) override { out += '\n'; }
  void visit(const Underline& u) override { u.visitKids(*this); }
};

struct AssExporter : Visitor {      // 导出成 ASS 的覆盖标签
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
  void visit(const Italic& i) override { out += "{\\i1}"; i.visitKids(*this); out += "{\\i0}"; }
  void visit(const Color& c) override {
    char tag[32];                     // ASS 的颜色是 &HBBGGRR&
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c.rgb & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb >> 16);
    out += tag; c.visitKids(*this); out += "{\\c}";
  }
  void visit(const Br&) override { out += "\\N"; }
  void visit(const Underline& u) override { out += "{\\u1}"; u.visitKids(*this); out += "{\\u0}"; }
};

// ---- 以下是测试
struct Cue {                        // 一条字幕
  Children items;
  void accept(Visitor& v) const { for (auto& n : items) n->accept(v); }
};

std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

Cue sample() {   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ <u>再见</u>
  Cue c;
  c.items.push_back(text("Hello "));
  c.items.push_back(span(Bold{}, text("big")));
  c.items.push_back(text(" "));
  c.items.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  c.items.push_back(std::make_unique<Br>());
  c.items.push_back(span(Underline{}, text("再见")));
  return c;
}

int main() {
  Cue cue = sample();
  CharCounter cc;   cue.accept(cc);
  PlainExporter pe; cue.accept(pe);
  AssExporter ae;   cue.accept(ae);
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", cc.n, pe.out.c_str(), ae.out.c_str());
}
