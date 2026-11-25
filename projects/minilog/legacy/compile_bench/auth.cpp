#ifdef USE_PIMPL
#include "minilog/logger.h"
void runAuth(minilog::Logger& log) { log.info("auth token verified"); }
#else
#include "monolithic_logger.h"
void runAuth() { MonoLogger::instance().log(MonoLogger::Info, "auth", "auth token verified"); }
#endif
