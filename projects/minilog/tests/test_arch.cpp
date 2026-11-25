#include "minilog/logger.h"
#include "minilog/scope_guard.h"
#include "minilog/sink.h"
#include "test_common.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace {

void testBuilderValidation() {
  {
    auto res = minilog::LoggerBuilder().addConsoleSink().build();
    MINILOG_CHECK(!res);
    MINILOG_CHECK(res.error.find("name") != std::string::npos);
  }
  {
    auto res = minilog::LoggerBuilder().name("app").build();
    MINILOG_CHECK(!res);
    MINILOG_CHECK(res.error.find("sink") != std::string::npos);
  }
  {
    auto res = minilog::LoggerBuilder()
                   .name("app")
                   .pattern("[%T] %Q %v")
                   .addConsoleSink()
                   .build();
    MINILOG_CHECK(!res);
    MINILOG_CHECK(res.error.find("%Q") != std::string::npos);
  }
  {
    auto res = minilog::LoggerBuilder()
                   .name("app")
                   .pattern("[%T] %")
                   .addConsoleSink()
                   .build();
    MINILOG_CHECK(!res);
  }
  {
    auto res = minilog::LoggerBuilder()
                   .name("app")
                   .addConsoleSink()
                   .async(true, 0)
                   .build();
    MINILOG_CHECK(!res);
    MINILOG_CHECK(res.error.find("queueCapacity") != std::string::npos);
  }
  {
    auto res = minilog::LoggerBuilder()
                   .name("app")
                   .addFileSink("")
                   .build();
    MINILOG_CHECK(!res);
  }
}

void testRegistryIsolation() {
  minilog::LoggerRegistry regA;
  minilog::LoggerRegistry regB;

  auto memA = std::make_shared<minilog::MemorySink>();
  auto memB = std::make_shared<minilog::MemorySink>();

  auto rA = minilog::LoggerBuilder()
                .name("svc")
                .level(minilog::Level::Debug)
                .addSink(minilog::AnySink(memA))
                .build();
  auto rB = minilog::LoggerBuilder()
                .name("svc")
                .level(minilog::Level::Warn)
                .addSink(minilog::AnySink(memB))
                .build();
  MINILOG_CHECK(rA && rB);

  std::shared_ptr<minilog::Logger> spA = std::move(rA.logger);
  std::shared_ptr<minilog::Logger> spB = std::move(rB.logger);

  MINILOG_CHECK(regA.registerLogger(spA));
  MINILOG_CHECK(!regA.registerLogger(spB));  // 同名拒绝重复注册
  MINILOG_CHECK(regB.registerLogger(spB));

  regA.get("svc")->logAt(minilog::Level::Debug, 1000, "only in A");
  regB.get("svc")->logAt(minilog::Level::Debug, 1000, "filtered in B");
  regB.get("svc")->logAt(minilog::Level::Error, 2000, "error in B");

  MINILOG_CHECK(memA->count() == 1);
  MINILOG_CHECK(memB->count() == 1);
  MINILOG_CHECK(regA.size() == 1);
  MINILOG_CHECK(regA.drop("svc"));
  MINILOG_CHECK(regA.get("svc") == nullptr);
  MINILOG_CHECK(regB.get("svc") != nullptr);
}

int simulateEarlyReturn(const std::string& logPath, bool failEarly, bool& rolledBack) {
  auto res = minilog::LoggerBuilder()
                 .name("tx")
                 .level(minilog::Level::Info)
                 .pattern("[%L] [%n] %v")
                 .addFileSink(logPath, minilog::Level::Info, 65536)
                 .build();
  MINILOG_CHECK(static_cast<bool>(res));

  auto txGuard = minilog::makeScopeGuard([&]() {
    rolledBack = true;
    res.logger->error("transaction rolled back");
  });

  res.logger->info("step 1 ok");
  if (failEarly) {
    return -1;  // 提前返回：ScopeGuard 记录回滚，随后 Logger RAII 析构自动刷盘
  }

  res.logger->info("step 2 committed");
  txGuard.dismiss();
  return 0;
}

void testRaiiAndScopeGuardFlush() {
  minilog_test::TempDirGuard dir("minilog_arch");
  const std::string path = dir.file("early.log");

  bool rolledBack = false;
  int rc = simulateEarlyReturn(path, true, rolledBack);
  MINILOG_CHECK(rc == -1);
  MINILOG_CHECK(rolledBack);

  std::ifstream in(path);
  MINILOG_CHECK(in.good());
  std::string line1, line2;
  MINILOG_CHECK(static_cast<bool>(std::getline(in, line1)));
  MINILOG_CHECK(static_cast<bool>(std::getline(in, line2)));
  MINILOG_CHECK(line1 == "[INFO] [tx] step 1 ok");
  MINILOG_CHECK(line2 == "[ERROR] [tx] transaction rolled back");

  bool committedRollback = false;
  const std::string path2 = dir.file("commit.log");
  MINILOG_CHECK(simulateEarlyReturn(path2, false, committedRollback) == 0);
  MINILOG_CHECK(!committedRollback);
}

}  // namespace

int main() {
  testBuilderValidation();
  testRegistryIsolation();
  testRaiiAndScopeGuardFlush();
  std::printf("test_arch passed\n");
  return 0;
}
