// 《单例》：两个测试共用一个单例，测试的结果取决于运行顺序
#include <iostream>
#include <string>

class Config {
public:
    static Config& instance() { static Config c; return c; }
    double discount = 0.0;                     // 默认不打折
private:
    Config() = default;
};

// 被测的业务函数：悄悄依赖了全局的 Config
double finalPrice(double price) { return price * (1 - Config::instance().discount); }

bool testVipDiscount() {
    Config::instance().discount = 0.2;         // 为了测 VIP 折扣，改了全局配置，测完忘了改回去
    return finalPrice(100) == 80;
}
bool testNormalPrice() {
    return finalPrice(100) == 100;             // 以为没有折扣
}

int main() {
    // 按「普通价格 → VIP 折扣」的顺序运行：都通过
    std::cout << "顺序一：普通价格 " << (testNormalPrice() ? "✓" : "✗") << "，VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << '\n';
    // 测试框架换了一个顺序（或者并行运行）：普通价格的测试失败了
    std::cout << "顺序二：VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << "，普通价格 " << (testNormalPrice() ? "✓" : "✗") << '\n';
}
