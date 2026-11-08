#pragma once
#include "printer.h"

class SimplePrinter : public Printer {       // 只实现自己真正支持的接口
public:
    void print(const Doc& d) override;
};
