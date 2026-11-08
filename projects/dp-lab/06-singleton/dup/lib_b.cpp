#include "registry.h"
extern "C" API void* registry_from_b() { Registry::instance().count++; return &Registry::instance(); }
