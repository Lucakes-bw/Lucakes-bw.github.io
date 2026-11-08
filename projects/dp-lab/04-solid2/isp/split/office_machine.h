#pragma once
#include "fax.h"
#include "printer.h"
#include "scanner.h"

class OfficeMachine : public Printer, public Scanner, public Fax {   // 一体机实现三个小接口
public:
    void print(const Doc& d) override;
    Doc scan() override;
    void fax(const Doc& d, const std::string& number) override;
};
