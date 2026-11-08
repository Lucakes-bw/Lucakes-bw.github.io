#include "registry.h"
extern "C" API void* registry_from_a() { Registry::instance().count++; return &Registry::instance(); }
