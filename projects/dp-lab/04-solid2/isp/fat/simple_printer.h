#pragma once
#include "device.h"

class SimplePrinter : public Device {        // 普通打印机：只会打印，另外两个只能写成「不支持」
public:
    void print(const Doc& d) override;
    Doc scan() override;
    void fax(const Doc& d, const std::string& number) override;
};
