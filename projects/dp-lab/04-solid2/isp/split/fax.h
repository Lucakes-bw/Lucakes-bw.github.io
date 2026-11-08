#pragma once
#include <string>
#include "doc.h"
class Fax { public: virtual ~Fax() = default; virtual void fax(const Doc& d, const std::string& number) = 0; };
