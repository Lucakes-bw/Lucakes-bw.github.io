#include "office_machine.h"
#include <iostream>

void OfficeMachine::print(const Doc& d) { std::cout << "[一体机] 打印 " << d.text << '\n'; }
Doc OfficeMachine::scan() { return {"扫描件"}; }
void OfficeMachine::fax(const Doc& d, const std::string& number) { std::cout << "[一体机] 传真 " << d.text << " 到 " << number << '\n'; }
