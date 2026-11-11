// 《访问者模式》练习 4：加了 Underline，访问者接口和每个访问者都改了，编译也通过了，可下划线导出成了粗体
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Text; struct Bold; struct Underline; struct Br;
struct Visitor {
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Underline&) = 0;
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
struct Bold : Node {
  Children kids;
  void accept(Visitor& v) const override { v.visit(*this); }
  void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
};
struct Underline : Bold {};                       // 和 Bold 一样有子节点，直接继承过来
struct Br : Node { void accept(Visitor& v) const override { v.visit(*this); } };

struct AssExporter : Visitor {
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
  void visit(const Underline& u) override { out += "{\\u1}"; u.visitKids(*this); out += "{\\u0}"; }
  void visit(const Br&) override { out += "\\N"; }
};

int main() {
  Children cue;
  auto b = std::make_unique<Bold>();
  b->kids.push_back(std::make_unique<Text>("Hello"));
  auto u = std::make_unique<Underline>();
  u->kids.push_back(std::make_unique<Text>("再见"));
  cue.push_back(std::move(b));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(std::move(u));
  AssExporter a;
  for (auto& n : cue) n->accept(a);
  std::printf("ASS：%s\n", a.out.c_str());
}
