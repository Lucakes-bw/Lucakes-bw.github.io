#include "office_machine.h"
#include "simple_printer.h"
void print_invoice(Device&);
void print_report(Device&);
void print_label(Device&);
void print_receipt(Device&);
Doc scan_contract(Device&);
void fax_order(Device&);

int main() {
    SimplePrinter printer;
    OfficeMachine machine;
    print_invoice(printer);
    print_report(printer);
    print_label(printer);
    print_receipt(printer);
    scan_contract(machine);
    fax_order(machine);
    fax_order(printer);         // 编译得过，运行时才发现「不支持」
}
