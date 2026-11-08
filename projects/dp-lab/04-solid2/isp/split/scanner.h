#pragma once
#include "doc.h"
class Scanner { public: virtual ~Scanner() = default; virtual Doc scan() = 0; };
