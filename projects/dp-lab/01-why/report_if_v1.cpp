// 《为什么需要设计模式》：if-else 版报表，支持 CSV 和 JSON
#include <iostream>
#include <string>
#include <vector>

struct Sale {
    std::string region, product;
    int qty;
    double price;
};

void printHeader(const std::string& fmt) {
    if (fmt == "csv") {
        std::cout << "region,product,qty,price\n";
    } else if (fmt == "json") {
        std::cout << "[\n";
    }
}

void printRow(const std::string& fmt, const Sale& s, bool last) {
    if (fmt == "csv") {
        std::cout << s.region << ',' << s.product << ',' << s.qty << ',' << s.price << '\n';
    } else if (fmt == "json") {
        std::cout << "  {\"region\": \"" << s.region << "\", \"product\": \"" << s.product
                  << "\", \"qty\": " << s.qty << ", \"price\": " << s.price << "}" << (last ? "" : ",") << '\n';
    }
}

void printFooter(const std::string& fmt) {
    if (fmt == "json") {
        std::cout << "]\n";
    }
}

void report(const std::string& fmt, const std::vector<Sale>& sales) {
    printHeader(fmt);
    for (size_t i = 0; i < sales.size(); ++i) printRow(fmt, sales[i], i + 1 == sales.size());
    printFooter(fmt);
}

int main() {
    std::vector<Sale> sales = {{"east", "keyboard", 12, 199}, {"south", "mouse", 30, 59.5}};
    for (const char* fmt : {"csv", "json"}) {
        std::cout << "== " << fmt << " ==\n";
        report(fmt, sales);
    }
}
