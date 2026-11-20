// 《解释器模式》练习 3：运算符优先级写扁 + 提前算完左右分支破坏短路求值
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip { std::string name; int width, fps; std::string codec; };

struct Expr { virtual ~Expr() = default; virtual bool eval(const Clip&, int& probes) const = 0; };
using ExprPtr = std::unique_ptr<Expr>;

struct CodecEq : Expr {
  std::string val; explicit CodecEq(std::string v) : val(std::move(v)) {}
  bool eval(const Clip& c, int& probes) const override { ++probes; return c.codec == val; }
};
struct GeExpr : Expr {
  bool isWidth; int val; GeExpr(bool w, int v) : isWidth(w), val(v) {}
  bool eval(const Clip& c, int& probes) const override { ++probes; return (isWidth ? c.width : c.fps) >= val; }
};

struct BadAnd : Expr {
  ExprPtr l, r; BadAnd(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c, int& probes) const override {
    bool a = l->eval(c, probes), b = r->eval(c, probes);  // 坑 1：先算两边，短路失效
    return a && b;
  }
};
struct BadOr : Expr {
  ExprPtr l, r; BadOr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c, int& probes) const override {
    bool a = l->eval(c, probes), b = r->eval(c, probes);  // 同样破坏短路
    return a || b;
  }
};

// 坑 2：把 and / or 放在同一个 while 循环里从左到右结合，无视 and 优先级高于 or
ExprPtr parseFlat(const std::vector<std::string>& ops, std::vector<ExprPtr> atoms) {
  ExprPtr e = std::move(atoms[0]);
  for (size_t i = 0; i < ops.size(); ++i)
    e = (ops[i] == "and") ? ExprPtr(std::make_unique<BadAnd>(std::move(e), std::move(atoms[i + 1])))
                          : ExprPtr(std::make_unique<BadOr>(std::move(e), std::move(atoms[i + 1])));
  return e;
}

int main() {
  // 表达式：codec == "hevc" or width >= 1920 and fps >= 60
  std::vector<ExprPtr> atoms;
  atoms.push_back(std::make_unique<CodecEq>("hevc"));
  atoms.push_back(std::make_unique<GeExpr>(true, 1920));
  atoms.push_back(std::make_unique<GeExpr>(false, 60));
  auto ast = parseFlat({"or", "and"}, std::move(atoms));

  Clip drone{"drone_hdr.mkv", 2560, 30, "hevc"};
  int probes = 0;
  bool ok = ast->eval(drone, probes);
  std::printf("%s 命中=%d，叶子求值次数=%d\n", drone.name.c_str(), ok ? 1 : 0, probes);
}
