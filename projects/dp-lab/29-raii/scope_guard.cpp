// 《RAII 与 Scope Guard》：通用 ScopeGuard（含 dismiss()）——事务回滚与临时状态恢复
#include <cstdio>
#include <string>
#include <utility>

template <typename F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F&& fn) noexcept : fn_(std::forward<F>(fn)), active_(true) {}
  ~ScopeGuard() { if (active_) fn_(); }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& o) noexcept
      : fn_(std::move(o.fn_)), active_(std::exchange(o.active_, false)) {}

  void dismiss() noexcept { active_ = false; }

 private:
  F fn_;
  bool active_;
};

template <typename F>
ScopeGuard<std::decay_t<F>> makeScopeGuard(F&& fn) {
  return ScopeGuard<std::decay_t<F>>(std::forward<F>(fn));
}

struct PipelineConfig {
  std::string codec = "H264";
  int logLevel = 1;
  bool tempFileExists = false;
};

int switchCodecTransaction(PipelineConfig& cfg, const std::string& nextCodec, bool encodeOk) {
  // 1. 无条件恢复临时状态（永远不 dismiss）
  int savedLog = cfg.logLevel;
  cfg.logLevel = 3;
  auto restoreLog = makeScopeGuard([&] {
    cfg.logLevel = savedLog;
    std::printf("  [guard] 恢复 logLevel=%d\n", cfg.logLevel);
  });

  // 2. 事务性回滚：只有全部成功才 dismiss()
  std::string oldCodec = cfg.codec;
  cfg.codec = nextCodec;
  cfg.tempFileExists = true;
  auto rollback = makeScopeGuard([&] {
    cfg.codec = oldCodec;
    cfg.tempFileExists = false;
    std::printf("  [guard] 失败回滚：codec=%s, 删除临时文件\n", cfg.codec.c_str());
  });

  if (!encodeOk) {
    std::printf("  [work]  编码校验失败，提前 return -1\n");
    return -1;                       // 未 dismiss：先执行 rollback，再执行 restoreLog
  }

  cfg.tempFileExists = false;
  rollback.dismiss();                // 提交成功：取消回滚，仅保留 restoreLog
  std::printf("  [work]  切换成功，已 dismiss 回滚守卫\n");
  return 0;
}

int main() {
  PipelineConfig cfg;
  std::printf("== 事务 1：失败路径 ==\n");
  switchCodecTransaction(cfg, "AV1", false);
  std::printf("结果：codec=%s logLevel=%d tempFile=%d\n",
              cfg.codec.c_str(), cfg.logLevel, cfg.tempFileExists);

  std::printf("== 事务 2：成功路径 ==\n");
  switchCodecTransaction(cfg, "HEVC", true);
  std::printf("结果：codec=%s logLevel=%d tempFile=%d\n",
              cfg.codec.c_str(), cfg.logLevel, cfg.tempFileExists);
}
