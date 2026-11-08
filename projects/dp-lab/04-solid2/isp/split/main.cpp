#include "office_machine.h"
#include "simple_printer.h"
void print_invoice(Printer&);
void print_report(Printer&);
void print_label(Printer&);
void print_receipt(Printer&);
Doc scan_contract(Scanner&);
void fax_order(Fax&);

int main() {
    SimplePrinter printer;
    OfficeMachine machine;
    print_invoice(printer);
    print_report(printer);
    print_label(printer);
    print_receipt(printer);
    scan_contract(machine);
    fax_order(machine);
    // fax_order(printer);      // 编译错误：SimplePrinter 不是 Fax
}
