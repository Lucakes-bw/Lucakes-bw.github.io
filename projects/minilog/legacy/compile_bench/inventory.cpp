#ifdef USE_PIMPL
#include "minilog/logger.h"
void runInventory(minilog::Logger& log) { log.info("inventory reserved sku=A100"); }
#else
#include "monolithic_logger.h"
void runInventory() { MonoLogger::instance().log(MonoLogger::Info, "inventory", "inventory reserved sku=A100"); }
#endif
