// 《访问者模式》练习 2：加一个访问者，算出字幕最宽那一行的显示宽度（ASCII 占 1 格，中文等宽字符占 2 格）
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Text; struct Bold; struct Italic; struct Color; struct Br;
struct Visitor {
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Italic&) = 0;
  virtual void visit(const Color&) = 0;
  virtual void visit(const Br&) = 0;
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
struct Span : Node {
  Children kids;
  void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
};
struct Bold : Span { void accept(Visitor& v) const override { v.visit(*this); } };
struct Italic : Span { void accept(Visitor& v) const override { v.visit(*this); } };
struct Color : Span {
  std::uint32_t rgb;
  explicit Color(std::uint32_t c) : rgb(c) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Br : Node { void accept(Visitor& v) const override { v.visit(*this); } };

// 新的访问者：不用改任何元素类。访问者可以带状态：当前行宽、最宽的行
struct WidthMeasurer : Visitor {
  int line = 0, widest = 0;
  void visit(const Text& t) override {
    for (unsigned char c : t.s) {
      if ((c & 0xC0) == 0x80) continue;          // UTF-8 的后续字节
      line += c < 0x80 ? 1 : 2;                  // 简化：非 ASCII 都按全角算
    }
    widest = std::max(widest, line);
  }
  void visit(const Bold& b) override { b.visitKids(*this); }
  void visit(const Italic& i) override { i.visitKids(*this); }
  void visit(const Color& c) override { c.visitKids(*this); }
  void visit(const Br&) override { line = 0; }   // 换行：从 0 重新数
};

std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

int main() {
  Children cue;     // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见，<b>大</b>千世界！
  cue.push_back(text("Hello "));
  cue.push_back(span(Bold{}, text("big")));
  cue.push_back(text(" "));
  cue.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(text("再见，"));
  cue.push_back(span(Bold{}, text("大")));
  cue.push_back(text("千世界！"));
  WidthMeasurer w;
  for (auto& n : cue) n->accept(w);
  std::printf("最宽一行占 %d 格\n", w.widest);
}
