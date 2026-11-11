// 《责任链模式》：不纯的责任链——日志沿「等级过滤 → 重复折叠 → 加前缀 → 输出」传递，每一环可以截住，也可以加工后继续传
#include <cstdio>
#include <string>

enum Level { kError = 16, kWarning = 24, kInfo = 32, kDebug = 48 };   // 数值和 FFmpeg 的 AV_LOG_* 一样
struct LogMsg { int level; std::string module, text; };

class LogHandler {
 public:
  virtual ~LogHandler() = default;
  LogHandler* setNext(LogHandler* next) { next_ = next; return next; }
  virtual void log(LogMsg& m) { if (next_) next_->log(m); }
 private:
  LogHandler* next_ = nullptr;
};

class LevelFilter : public LogHandler {     // 比设定等级啰嗦的消息直接截住
 public:
  explicit LevelFilter(int level) : level_(level) {}
  int dropped = 0;
  void log(LogMsg& m) override {
    if (m.level > level_) { ++dropped; return; }
    LogHandler::log(m);
  }
 private:
  int level_;
};

class RepeatFolder : public LogHandler {    // 连续重复的消息只放过第一条，换了内容再补一句「重复了 N 次」
 public:
  int dropped = 0;
  void log(LogMsg& m) override {
    if (m.text == last_) { ++repeat_; ++dropped; return; }
    flush();
    last_ = m.text;
    LogHandler::log(m);
  }
  void flush() {
    if (repeat_ == 0) return;
    LogMsg note{kInfo, "", "    Last message repeated " + std::to_string(repeat_) + " times"};
    repeat_ = 0;
    LogHandler::log(note);
  }
 private:
  std::string last_;
  int repeat_ = 0;
};

class Prefixer : public LogHandler {        // 加工后继续传：在前面加上 [模块名]
 public:
  void log(LogMsg& m) override {
    if (!m.module.empty()) m.text = "[" + m.module + "] " + m.text;
    LogHandler::log(m);
  }
};

class ConsoleSink : public LogHandler {     // 链尾：真正输出
 public:
  int printed = 0;
  void log(LogMsg& m) override { ++printed; std::printf("%s\n", m.text.c_str()); }
};

int main() {
  LevelFilter filter(kWarning);
  RepeatFolder folder;
  Prefixer prefix;
  ConsoleSink sink;
  filter.setNext(&folder)->setNext(&prefix)->setNext(&sink);

  LogMsg msgs[] = {
    {kInfo, "mov", "Stream #0:0: Video: h264, 1280x720"},
    {kWarning, "h264", "corrupted macroblock 12 34"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kDebug, "h264", "nal_unit_type: 1, nal_ref_idc: 2"},
    {kDebug, "h264", "nal_unit_type: 1, nal_ref_idc: 0"},
    {kWarning, "h264", "concealing 1532 DC, 1532 AC, 1532 MV errors"},
    {kInfo, "aac", "Input buffer exhausted before END element found"},
  };
  for (LogMsg& m : msgs) filter.log(m);
  folder.flush();
  std::printf("-- 共 %zu 条：等级过滤截住 %d 条，重复折叠截住 %d 条，输出 %d 行\n",
              sizeof msgs / sizeof msgs[0], filter.dropped, folder.dropped, sink.printed);
}
