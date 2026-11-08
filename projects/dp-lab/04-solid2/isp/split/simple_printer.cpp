#include "simple_printer.h"
#include <iostream>

void SimplePrinter::print(const Doc& d) { std::cout << "[打印机] 打印 " << d.text << '\n'; }
