#ifdef USE_PIMPL
#include "minilog/logger.h"
void runMetrics(minilog::Logger& log) { log.info("metrics flushed qps=12000"); }
#else
#include "monolithic_logger.h"
void runMetrics() { MonoLogger::instance().log(MonoLogger::Info, "metrics", "metrics flushed qps=12000"); }
#endif
