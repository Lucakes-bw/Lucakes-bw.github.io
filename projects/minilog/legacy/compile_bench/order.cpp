#ifdef USE_PIMPL
#include "minilog/logger.h"
void runOrder(minilog::Logger& log) { log.info("order created id=42"); }
#else
#include "monolithic_logger.h"
void runOrder() { MonoLogger::instance().log(MonoLogger::Info, "order", "order created id=42"); }
#endif
