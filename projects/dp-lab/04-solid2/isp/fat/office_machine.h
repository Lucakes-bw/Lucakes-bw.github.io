#pragma once
#include "device.h"

class OfficeMachine : public Device {        // 多功能一体机：三样都能做
public:
    void print(const Doc& d) override;
    Doc scan() override;
    void fax(const Doc& d, const std::string& number) override;
};
