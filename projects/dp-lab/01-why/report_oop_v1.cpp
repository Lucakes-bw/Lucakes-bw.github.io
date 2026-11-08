// 《为什么需要设计模式》：多态版报表，支持 CSV 和 JSON
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Sale {
    std::string region, product;
    int qty;
    double price;
};

// 每种格式要回答的三个问题：开头写什么、每行写什么、结尾写什么
class Formatter {
public:
    virtual ~Formatter() = default;
    virtual void header() const = 0;
    virtual void row(const Sale& s, bool last) const = 0;
    virtual void footer() const = 0;
};

class CsvFormatter : public Formatter {
public:
    void header() const override { std::cout << "region,product,qty,price\n"; }
    void row(const Sale& s, bool) const override {
        std::cout << s.region << ',' << s.product << ',' << s.qty << ',' << s.price << '\n';
    }
    void footer() const override {}
};

class JsonFormatter : public Formatter {
public:
    void header() const override { std::cout << "[\n"; }
    void row(const Sale& s, bool last) const override {
        std::cout << "  {\"region\": \"" << s.region << "\", \"product\": \"" << s.product
                  << "\", \"qty\": " << s.qty << ", \"price\": " << s.price << "}" << (last ? "" : ",") << '\n';
    }
    void footer() const override { std::cout << "]\n"; }
};

// 格式名 → 创建函数
std::unique_ptr<Formatter> makeFormatter(const std::string& fmt) {
    static const std::map<std::string, std::function<std::unique_ptr<Formatter>()>> table = {
        {"csv", [] { return std::make_unique<CsvFormatter>(); }},
        {"json", [] { return std::make_unique<JsonFormatter>(); }},
    };
    auto it = table.find(fmt);
    return it == table.end() ? nullptr : it->second();
}

void report(const Formatter& f, const std::vector<Sale>& sales) {
    f.header();
    for (size_t i = 0; i < sales.size(); ++i) f.row(sales[i], i + 1 == sales.size());
    f.footer();
}

int main() {
    std::vector<Sale> sales = {{"east", "keyboard", 12, 199}, {"south", "mouse", 30, 59.5}};
    for (const char* fmt : {"csv", "json"}) {
        std::cout << "== " << fmt << " ==\n";
        report(*makeFormatter(fmt), sales);
    }
}
