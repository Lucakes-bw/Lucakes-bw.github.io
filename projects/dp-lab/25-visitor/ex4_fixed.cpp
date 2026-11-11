// 《访问者模式》练习 4 的答案：accept() 由 CRTP 基类自动生成，具体元素类标成 final
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

// 每个具体元素都从 Visitable<自己> 派生，accept() 不用手写，也就不会忘
template <class Derived, class Base = Node>
struct Visitable : Base {
  void accept(Visitor& v) const override { v.visit(static_cast<const Derived&>(*this)); }
};
struct Span : Node {                              // 共用的部分放进一个不能直接访问的基类
  Children kids;
  void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
};

struct Text final : Visitable<Text> {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
};
struct Bold final : Visitable<Bold, Span> {};
struct Underline final : Visitable<Underline, Span> {};   // 写成 : Bold 会编译失败（Bold 是 final）
struct Br final : Visitable<Br> {};

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
