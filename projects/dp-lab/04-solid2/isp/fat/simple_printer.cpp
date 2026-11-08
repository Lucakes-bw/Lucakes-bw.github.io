#include "simple_printer.h"
#include <iostream>

void SimplePrinter::print(const Doc& d) { std::cout << "[打印机] 打印 " << d.text << '\n'; }
Doc SimplePrinter::scan() { std::cout << "[打印机] 不支持扫描\n"; return {}; }
void SimplePrinter::fax(const Doc&, const std::string&) { std::cout << "[打印机] 不支持传真\n"; }
