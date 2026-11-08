#pragma once
#include "doc.h"
class Printer { public: virtual ~Printer() = default; virtual void print(const Doc& d) = 0; };
