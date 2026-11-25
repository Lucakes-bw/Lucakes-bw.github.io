// 反面教材（6.4 对比）：过度 GoF 炫技版日志器
// 强行堆砌：全局单例 + 抽象工厂 + 5 层堆分配虚函数装饰器 + GoF 解释器 AST 类树（每次打日志解析并遍历）
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace overeng {

enum class Level { Trace = 0, Debug, Info, Warn, Error };

struct Context {
  uint64_t ts = 0;
  Level level = Level::Info;
  std::string module;
  std::string msg;
};

// 1. GoF 解释器类树（每个占位符一个堆对象，每次打日志现解析、虚调用 eval 拼字符串）
class Expr {
 public:
  virtual ~Expr() = default;
  virtual std::string eval(const Context& ctx) const = 0;
};

class LiteralExpr final : public Expr {
 public:
  explicit LiteralExpr(std::string s) : s_(std::move(s)) {}
  std::string eval(const Context&) const override { return s_; }
 private:
  std::string s_;
};

class TimeExpr final : public Expr {
 public:
  std::string eval(const Context& ctx) const override {
    return std::to_string(ctx.ts);
  }
};

class LevelExpr final : public Expr {
 public:
  std::string eval(const Context& ctx) const override {
    switch (ctx.level) {
      case Level::Trace: return "TRACE";
      case Level::Debug: return "DEBUG";
      case Level::Info:  return "INFO";
      case Level::Warn:  return "WARN";
      case Level::Error: return "ERROR";
    }
    return "INFO";
  }
};

class ModuleExpr final : public Expr {
 public:
  std::string eval(const Context& ctx) const override { return ctx.module; }
};

class MsgExpr final : public Expr {
 public:
  std::string eval(const Context& ctx) const override { return ctx.msg; }
};

class SequenceExpr final : public Expr {
 public:
  void add(std::unique_ptr<Expr> e) { children_.push_back(std::move(e)); }
  std::string eval(const Context& ctx) const override {
    std::string out;
    for (const auto& c : children_) out += c->eval(ctx);
    return out;
  }
 private:
  std::vector<std::unique_ptr<Expr>> children_;
};

inline std::unique_ptr<Expr> parsePatternAst(const std::string& pat) {
  auto seq = std::make_unique<SequenceExpr>();
  std::string lit;
  auto flushLit = [&]() {
    if (!lit.empty()) {
      seq->add(std::make_unique<LiteralExpr>(std::move(lit)));
      lit.clear();
    }
  };
  for (std::size_t i = 0; i < pat.size(); ++i) {
    if (pat[i] == '%' && i + 1 < pat.size()) {
      flushLit();
      char spec = pat[++i];
      if (spec == 'T') seq->add(std::make_unique<TimeExpr>());
      else if (spec == 'L') seq->add(std::make_unique<LevelExpr>());
      else if (spec == 'n') seq->add(std::make_unique<ModuleExpr>());
      else if (spec == 'v') seq->add(std::make_unique<MsgExpr>());
      else lit.push_back(spec);
    } else {
      lit.push_back(pat[i]);
    }
  }
  flushLit();
  return seq;
}

// 2. 抽象接口 + 5 层堆分配虚函数装饰器
class ISink {
 public:
  virtual ~ISink() = default;
  virtual void write(Context& ctx) = 0;
  virtual std::size_t count() const = 0;
  virtual std::size_t checksum() const = 0;
};

class Formatter {
 public:
  virtual ~Formatter() = default;
  virtual std::string format(const Context& ctx) const = 0;
};

class InterpreterFormatter final : public Formatter {
 public:
  explicit InterpreterFormatter(std::string pat) : pat_(std::move(pat)) {}
  std::string format(const Context& ctx) const override {
    // 故意在每条日志上重建 AST（典型过度/误用解释器的反面教材）
    auto ast = parsePatternAst(pat_);
    return ast->eval(ctx);
  }
 private:
  std::string pat_;
};

class CountingCoreSink final : public ISink {
 public:
  explicit CountingCoreSink(std::unique_ptr<Formatter> fmt) : fmt_(std::move(fmt)) {}
  void write(Context& ctx) override {
    std::string line = fmt_->format(ctx);
    ++count_;
    checksum_ += line.size();
  }
  std::size_t count() const override { return count_; }
  std::size_t checksum() const override { return checksum_; }
 private:
  std::unique_ptr<Formatter> fmt_;
  std::size_t count_ = 0;
  std::size_t checksum_ = 0;
};

class SinkDecorator : public ISink {
 public:
  explicit SinkDecorator(std::unique_ptr<ISink> inner) : inner_(std::move(inner)) {}
  std::size_t count() const override { return inner_->count(); }
  std::size_t checksum() const override { return inner_->checksum(); }
 protected:
  std::unique_ptr<ISink> inner_;
};

class LockingDecorator final : public SinkDecorator {
 public:
  using SinkDecorator::SinkDecorator;
  void write(Context& ctx) override {
    std::lock_guard<std::mutex> lock(mtx_);
    inner_->write(ctx);
  }
 private:
  std::mutex mtx_;
};

class LevelDecorator final : public SinkDecorator {
 public:
  LevelDecorator(std::unique_ptr<ISink> inner, Level minLvl)
      : SinkDecorator(std::move(inner)), min_(minLvl) {}
  void write(Context& ctx) override {
    if (ctx.level >= min_) inner_->write(ctx);
  }
 private:
  Level min_;
};

class ModuleDecorator final : public SinkDecorator {
 public:
  ModuleDecorator(std::unique_ptr<ISink> inner, std::string allow)
      : SinkDecorator(std::move(inner)), allow_(std::move(allow)) {}
  void write(Context& ctx) override {
    if (ctx.module == allow_) inner_->write(ctx);
  }
 private:
  std::string allow_;
};

class RedactDecorator final : public SinkDecorator {
 public:
  using SinkDecorator::SinkDecorator;
  void write(Context& ctx) override {
    std::size_t pos = ctx.msg.find("token=");
    if (pos != std::string::npos) {
      std::size_t end = pos + 6;
      while (end < ctx.msg.size() && std::isalnum(static_cast<unsigned char>(ctx.msg[end]))) {
        ++end;
      }
      ctx.msg.replace(pos + 6, end - (pos + 6), "***");
    }
    inner_->write(ctx);
  }
};

class MetricsDecorator final : public SinkDecorator {
 public:
  using SinkDecorator::SinkDecorator;
  void write(Context& ctx) override {
    ++seen_;
    inner_->write(ctx);
  }
 private:
  std::size_t seen_ = 0;
};

// 3. 抽象工厂（硬把正交的 Sink 与 Formatter 绑成产品族）
class LogSuiteFactory {
 public:
  virtual ~LogSuiteFactory() = default;
  virtual std::unique_ptr<Formatter> createFormatter() const = 0;
  virtual std::unique_ptr<ISink> createPipeline() const = 0;
};

class StandardSuiteFactory final : public LogSuiteFactory {
 public:
  std::unique_ptr<Formatter> createFormatter() const override {
    return std::make_unique<InterpreterFormatter>("[%T] [%L] [%n] %v");
  }
  std::unique_ptr<ISink> createPipeline() const override {
    auto core = std::make_unique<CountingCoreSink>(createFormatter());
    auto d1 = std::make_unique<RedactDecorator>(std::move(core));
    auto d2 = std::make_unique<ModuleDecorator>(std::move(d1), "payment");
    auto d3 = std::make_unique<LevelDecorator>(std::move(d2), Level::Info);
    auto d4 = std::make_unique<LockingDecorator>(std::move(d3));
    return std::make_unique<MetricsDecorator>(std::move(d4));
  }
};

// 4. 全局单例门面
class OverEngineeredLogger {
 public:
  static OverEngineeredLogger& instance() {
    static OverEngineeredLogger g;
    return g;
  }
  void init(const LogSuiteFactory& factory) { root_ = factory.createPipeline(); }
  void log(Level lvl, std::string mod, std::string msg) {
    Context ctx{1000, lvl, std::move(mod), std::move(msg)};
    if (root_) root_->write(ctx);
  }
  std::size_t count() const { return root_ ? root_->count() : 0; }
  std::size_t checksum() const { return root_ ? root_->checksum() : 0; }
 private:
  OverEngineeredLogger() {
    StandardSuiteFactory factory;
    init(factory);
  }
  std::unique_ptr<ISink> root_;
};

}  // namespace overeng

#ifndef OVERENG_NO_MAIN
int main() {
  auto& log = overeng::OverEngineeredLogger::instance();
  for (int i = 0; i < 1000; ++i) {
    log.log(overeng::Level::Info, "payment", "order ok token=secret99");
  }
  std::printf("overengineered count=%zu checksum=%zu\n", log.count(), log.checksum());
  return 0;
}
#endif
