// 《访问者模式》：重载不是分派：按参数的静态类型选重载，手里只有 Node& 时永远选中 visit(const Node&)
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node { virtual ~Node() = default; };
struct Text : Node { std::string s; explicit Text(std::string t) : s(std::move(t)) {} };
struct Br : Node {};

struct AssExporter {
  std::string out;
  void visit(const Text& t) { out += t.s; }
  void visit(const Br&) { out += "\\N"; }
  void visit(const Node&) { out += "[?]"; }      // 兜底
};

int main() {
  std::vector<std::unique_ptr<Node>> cue;
  cue.push_back(std::make_unique<Text>("Hello"));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(std::make_unique<Text>("再见"));

  AssExporter a;
  for (auto& n : cue) a.visit(*n);               // *n 的静态类型是 Node&
  std::printf("按重载：%s\n", a.out.c_str());

  AssExporter b;
  Text hello("Hello");
  Br br;
  b.visit(hello);                                // 静态类型就是 Text&、Br&，才选得对
  b.visit(br);
  std::printf("静态类型已知：%s\n", b.out.c_str());
}
