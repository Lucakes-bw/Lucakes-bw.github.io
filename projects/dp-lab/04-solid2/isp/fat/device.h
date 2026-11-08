// 胖接口：打印、扫描、传真全在一个接口里
#pragma once
#include <string>
#include "doc.h"

class Device {
public:
    virtual ~Device() = default;
    virtual void print(const Doc& d) = 0;
    virtual Doc scan() = 0;
    virtual void fax(const Doc& d, const std::string& number) = 0;
};
