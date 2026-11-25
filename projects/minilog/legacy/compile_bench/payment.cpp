#ifdef USE_PIMPL
#include "minilog/logger.h"
void runPayment(minilog::Logger& log) { log.warn("payment retry latency=180ms"); }
#else
#include "monolithic_logger.h"
void runPayment() { MonoLogger::instance().log(MonoLogger::Warn, "payment", "payment retry latency=180ms"); }
#endif
