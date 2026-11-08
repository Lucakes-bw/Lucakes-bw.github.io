// 《为什么需要设计模式》：多态版报表，再加一个新操作：每种格式的文件扩展名
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
    virtual std::string extension() const = 0;
};

class CsvFormatter : public Formatter {
public:
    void header() const override { std::cout << "region,product,qty,price\n"; }
    void row(const Sale& s, bool) const override {
        std::cout << s.region << ',' << s.product << ',' << s.qty << ',' << s.price << '\n';
    }
    void footer() const override {}
    std::string extension() const override { return ".csv"; }
};

class JsonFormatter : public Formatter {
public:
    void header() const override { std::cout << "[\n"; }
    void row(const Sale& s, bool last) const override {
        std::cout << "  {\"region\": \"" << s.region << "\", \"product\": \"" << s.product
                  << "\", \"qty\": " << s.qty << ", \"price\": " << s.price << "}" << (last ? "" : ",") << '\n';
    }
    void footer() const override { std::cout << "]\n"; }
    std::string extension() const override { return ".json"; }
};

class HtmlFormatter : public Formatter {
public:
    void header() const override {
        std::cout << "<table>\n  <tr><th>region</th><th>product</th><th>qty</th><th>price</th></tr>\n";
    }
    void row(const Sale& s, bool) const override {
        std::cout << "  <tr><td>" << s.region << "</td><td>" << s.product << "</td><td>" << s.qty
                  << "</td><td>" << s.price << "</td></tr>\n";
    }
    void footer() const override { std::cout << "</table>\n"; }
    std::string extension() const override { return ".html"; }
};

// 格式名 → 创建函数
std::unique_ptr<Formatter> makeFormatter(const std::string& fmt) {
    static const std::map<std::string, std::function<std::unique_ptr<Formatter>()>> table = {
        {"csv", [] { return std::make_unique<CsvFormatter>(); }},
        {"json", [] { return std::make_unique<JsonFormatter>(); }},
        {"html", [] { return std::make_unique<HtmlFormatter>(); }},
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
    for (const char* fmt : {"csv", "json", "html"}) {
        auto f = makeFormatter(fmt);
        std::cout << "== report" << f->extension() << " ==\n";
        report(*f, sales);
    }
}
