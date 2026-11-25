#include "minilog/filter.h"
#include "minilog/logger.h"
#include "minilog/pattern.h"
#include "minilog/record.h"
#include "minilog/sink.h"
#include "test_common.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace third_party {

struct AlertWebhook {
  int sent = 0;
  std::string lastMsg;
};

// 非侵入式 ADL 定制点：不修改 AlertWebhook 定义也不让它继承任何基类
void write_sink(AlertWebhook& hook, const minilog::LogRecord& rec) {
  ++hook.sent;
  hook.lastMsg.assign(rec.message().data(), rec.message().size());
}

}  // namespace third_party

namespace {

struct SmallMemberSink {
  int count = 0;
  void write(const minilog::LogRecord&) { ++count; }
};

struct LargeCustomSink {
  char padding[96]{};
  int count = 0;
  void write(const minilog::LogRecord&) { ++count; }
};

void testFilterPipeline() {
  minilog::FilterPipeline pipe;
  pipe.add(minilog::makeLevelFilter(minilog::Level::Info));
  pipe.add(minilog::makeModuleFilter({"auth", "pay"}, true));
  pipe.add(minilog::makeRateLimitFilter(2, 1000));
  pipe.add(minilog::makeRedactFilter());

  auto makeRec = [](uint64_t ts, minilog::Level lvl, const char* mod, const char* msg) {
    minilog::LogRecord r;
    r.timestampMs = ts;
    r.level = lvl;
    r.setLoggerName(mod);
    r.setMessage(msg);
    return r;
  };

  auto rDebug = makeRec(100, minilog::Level::Debug, "auth", "debug");
  MINILOG_CHECK(!pipe.apply(rDebug));

  auto rDeny = makeRec(100, minilog::Level::Info, "debug_mod", "ignored");
  MINILOG_CHECK(!pipe.apply(rDeny));

  auto r1 = makeRec(100, minilog::Level::Info, "auth", "user 13812345678 token=Abc123_x ok");
  MINILOG_CHECK(pipe.apply(r1));
  MINILOG_CHECK(r1.message() == "user 138****5678 token=*** ok");

  auto r2 = makeRec(200, minilog::Level::Info, "pay", "clean message");
  MINILOG_CHECK(pipe.apply(r2));
  MINILOG_CHECK(!r2.isHeap());  // 未发生脱敏改写且 <= 128 B，保持栈内联

  auto r3Limited = makeRec(500, minilog::Level::Error, "pay", "third in window");
  MINILOG_CHECK(!pipe.apply(r3Limited));

  auto r4NextWin = makeRec(1100, minilog::Level::Info, "pay", "next window");
  MINILOG_CHECK(pipe.apply(r4NextWin));
}

void testRotatingFileSink() {
  minilog_test::TempDirGuard dir("minilog_rot");
  const std::string base = dir.file("app.log");

  // 按行数策略：每个文件最多 3 行，保留 2 个归档
  {
    auto policy = std::make_unique<minilog::LineCountRotation>(3);
    minilog::RotatingFileSink sink(base, std::move(policy), 2, minilog::Level::Trace, "[%n] %v");
    MINILOG_CHECK(sink.isOpen());

    for (int i = 1; i <= 7; ++i) {
      minilog::LogRecord rec;
      rec.timestampMs = static_cast<uint64_t>(i * 100);
      rec.level = minilog::Level::Info;
      rec.setLoggerName("rot");
      rec.setMessage("line_" + std::to_string(i));
      sink.log(rec);
    }
    sink.flush();
    MINILOG_CHECK(sink.rotateCount() == 2);
  }

  MINILOG_CHECK(std::filesystem::exists(base));
  MINILOG_CHECK(std::filesystem::exists(base + ".1"));
  MINILOG_CHECK(std::filesystem::exists(base + ".2"));

  // 按大小策略
  const std::string sizeBase = dir.file("size.log");
  {
    auto policy = std::make_unique<minilog::SizeRotation>(40);
    minilog::RotatingFileSink sink(sizeBase, std::move(policy), 2, minilog::Level::Trace, "%v");
    for (int i = 0; i < 6; ++i) {
      minilog::LogRecord rec;
      rec.level = minilog::Level::Info;
      rec.setMessage("0123456789abcdef");  // 16 + '\n' = 17 B，每文件最多 2 行 (34 B)
      sink.log(rec);
    }
    sink.flush();
    MINILOG_CHECK(sink.rotateCount() == 2);
  }
  MINILOG_CHECK(std::filesystem::exists(sizeBase + ".2"));
}

void testAnySinkAndFactory() {
  minilog::AnySink adlSink(third_party::AlertWebhook{});
  MINILOG_CHECK(adlSink.isInline());

  minilog::AnySink memberSink(SmallMemberSink{});
  MINILOG_CHECK(memberSink.isInline());

  minilog::AnySink largeSink(LargeCustomSink{});
  MINILOG_CHECK(!largeSink.isInline());  // 超过 48 B，自动退回堆分配

  minilog::AnySink movedLarge = std::move(largeSink);
  MINILOG_CHECK(!largeSink);
  MINILOG_CHECK(!movedLarge.isInline());

  auto mem = std::make_shared<minilog::MemorySink>(minilog::Level::Trace, "[%n] %v");
  minilog::AnySink sharedWrapped(mem);
  MINILOG_CHECK(sharedWrapped.isInline());  // shared_ptr 为 16 B，直接放进 SBO

  auto res = minilog::LoggerBuilder()
                 .name("pipe")
                 .addSink(std::move(adlSink))
                 .addSink(std::move(movedLarge))
                 .addSink(std::move(sharedWrapped))
                 .addRedactFilter()
                 .build();
  MINILOG_CHECK(static_cast<bool>(res));
  res.logger->info("call 13900001234");
  MINILOG_CHECK(mem->count() == 1);
  MINILOG_CHECK(mem->lines()[0] == "[pipe] call 139****1234");

  // 测试 SinkFactory 注册表与自定义类型扩展
  minilog::SinkFactory factory;
  MINILOG_CHECK(factory.hasType("console"));
  MINILOG_CHECK(factory.hasType("rotating"));
  factory.registerType("small_member", [](const minilog::SinkSpec&, minilog::AnySink& out, std::string&) {
    out = minilog::AnySink(SmallMemberSink{});
    return true;
  });

  minilog::AnySink created;
  std::string err;
  minilog::SinkSpec customSpec;
  customSpec.type = "small_member";
  MINILOG_CHECK(factory.create(customSpec, created, err));
  MINILOG_CHECK(created.isInline());
  minilog::SinkSpec badSpec;
  badSpec.type = "nonexistent";
  MINILOG_CHECK(!factory.create(badSpec, created, err));
}

}  // namespace

int main() {
  testFilterPipeline();
  testRotatingFileSink();
  testAnySinkAndFactory();
  std::printf("test_pipeline passed\n");
  return 0;
}
