#include <cstdio>

#ifdef USE_PIMPL
#include "minilog/logger.h"
void runAuth(minilog::Logger&);
void runOrder(minilog::Logger&);
void runPayment(minilog::Logger&);
void runInventory(minilog::Logger&);
void runNotify(minilog::Logger&);
void runMetrics(minilog::Logger&);

int main() {
  auto res = minilog::LoggerBuilder()
                 .name("app")
                 .level(minilog::Level::Info)
                 .addConsoleSink(minilog::Level::Error)
                 .build();
  if (!res) return 1;
  runAuth(*res.logger);
  runOrder(*res.logger);
  runPayment(*res.logger);
  runInventory(*res.logger);
  runNotify(*res.logger);
  runMetrics(*res.logger);
  std::printf("sizeof(minilog::Logger) = %zu B\n", sizeof(minilog::Logger));
  return 0;
}
#else
#include "monolithic_logger.h"
void runAuth();
void runOrder();
void runPayment();
void runInventory();
void runNotify();
void runMetrics();

int main() {
  runAuth();
  runOrder();
  runPayment();
  runInventory();
  runNotify();
  runMetrics();
  std::printf("sizeof(MonoLogger) = %zu B\n", sizeof(MonoLogger));
  return 0;
}
#endif
