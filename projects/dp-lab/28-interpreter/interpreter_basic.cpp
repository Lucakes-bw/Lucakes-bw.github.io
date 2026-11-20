// 《解释器模式》：经典实现——Expr 抽象表达式 + 终结符/非终结符 + 极简递归下降解析器
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct Clip {
  std::string name;
  int width, fps, dur;
  std::string codec;
  bool hdr;
};

struct Expr { virtual ~Expr() = default; virtual bool eval(const Clip&) const = 0; };
using ExprPtr = std::unique_ptr<Expr>;

struct HdrExpr : Expr { bool eval(const Clip& c) const override { return c.hdr; } };

struct CmpExpr : Expr {                                   // 终结符：字段比较
  std::string field, op, sval; int ival = 0;
  CmpExpr(std::string f, std::string o, std::string v) : field(std::move(f)), op(std::move(o)), sval(std::move(v)) {
    if (field != "codec") ival = std::stoi(sval);
  }
  bool eval(const Clip& c) const override {
    if (field == "codec") return op == "==" ? c.codec == sval : c.codec != sval;
    int lhs = (field == "width") ? c.width : (field == "fps") ? c.fps : c.dur;
    if (op == "==") return lhs == ival; if (op == "!=") return lhs != ival;
    if (op == "<")  return lhs <  ival; if (op == "<=") return lhs <= ival;
    if (op == ">")  return lhs >  ival; return lhs >= ival;
  }
};

struct NotExpr : Expr {                                   // 非终结符：逻辑非
  ExprPtr sub; explicit NotExpr(ExprPtr s) : sub(std::move(s)) {}
  bool eval(const Clip& c) const override { return !sub->eval(c); }
};
struct AndExpr : Expr {                                   // 非终结符：逻辑与（短路）
  ExprPtr l, r; AndExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c) const override { return l->eval(c) && r->eval(c); }
};
struct OrExpr : Expr {                                    // 非终结符：逻辑或（短路）
  ExprPtr l, r; OrExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c) const override { return l->eval(c) || r->eval(c); }
};

struct Parser {
  std::vector<std::string> tok; size_t pos = 0;
  explicit Parser(const std::string& src) {
    for (size_t i = 0; i < src.size(); ) {
      if (std::isspace((unsigned char)src[i])) { ++i; continue; }
      if (src[i] == '(' || src[i] == ')') { tok.emplace_back(1, src[i++]); continue; }
      if (src[i] == '"') { size_t j = src.find('"', ++i); tok.push_back(src.substr(i, j - i)); i = j + 1; continue; }
      if (std::strchr("=!<>", src[i])) {
        size_t j = i + 1; if (j < src.size() && src[j] == '=') ++j;
        tok.push_back(src.substr(i, j - i)); i = j; continue;
      }
      size_t j = i; while (j < src.size() && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
      tok.push_back(src.substr(i, j - i)); i = j;
    }
  }
  bool match(const char* s) { return pos < tok.size() && tok[pos] == s ? (++pos, true) : false; }
  ExprPtr parseOr()  { auto e = parseAnd(); while (match("or"))  e = std::make_unique<OrExpr>(std::move(e), parseAnd()); return e; }
  ExprPtr parseAnd() { auto e = parseNot(); while (match("and")) e = std::make_unique<AndExpr>(std::move(e), parseNot()); return e; }
  ExprPtr parseNot() { return match("not") ? std::make_unique<NotExpr>(parseNot()) : parseAtom(); }
  ExprPtr parseAtom() {
    if (match("(")) { auto e = parseOr(); match(")"); return e; }
    if (match("hdr")) return std::make_unique<HdrExpr>();
    std::string f = tok[pos++], op = tok[pos++], v = tok[pos++];
    return std::make_unique<CmpExpr>(std::move(f), std::move(op), std::move(v));
  }
};

int main() {
  std::vector<Clip> clips = {
      {"intro_4k.mov",   3840, 60,  18, "hevc", true},
      {"interview.mp4",  1920, 30, 120, "h264", false},
      {"broll_slow.mp4", 1920, 60,  12, "h264", false},
      {"drone_hdr.mkv",  2560, 30,  25, "hevc", true},
      {"proxy_720.mp4",  1280, 24,   9, "h264", false},
  };
  for (const char* q : {"(width >= 1920 and fps >= 60) or codec == \"hevc\"", "not hdr and dur < 30"}) {
    auto ast = Parser(q).parseOr();
    std::printf("查询 %s ->", q);
    for (const auto& c : clips) if (ast->eval(c)) std::printf(" %s", c.name.c_str());
    std::printf("\n");
  }
}
