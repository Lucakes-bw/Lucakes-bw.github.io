#ifdef USE_PIMPL
#include "minilog/logger.h"
void runNotify(minilog::Logger& log) { log.info("notify webhook queued"); }
#else
#include "monolithic_logger.h"
void runNotify() { MonoLogger::instance().log(MonoLogger::Info, "notify", "notify webhook queued"); }
#endif
