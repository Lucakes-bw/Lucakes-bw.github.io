// 《单例》：改成依赖注入，每个测试用自己的 Config，互不影响
#include <iostream>

struct Config {
    double discount = 0.0;
};

double finalPrice(double price, const Config& cfg) { return price * (1 - cfg.discount); }   // 依赖写在参数里

bool testVipDiscount() {
    Config cfg;
    cfg.discount = 0.2;
    return finalPrice(100, cfg) == 80;
}
bool testNormalPrice() {
    Config cfg;
    return finalPrice(100, cfg) == 100;
}

int main() {
    std::cout << "顺序一：普通价格 " << (testNormalPrice() ? "✓" : "✗") << "，VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << '\n';
    std::cout << "顺序二：VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << "，普通价格 " << (testNormalPrice() ? "✓" : "✗") << '\n';

    // 真正运行时，程序在 main 里只创建一个 Config，把它传给需要的地方：
    // 「只有一个」是程序的决定，而不是 Config 这个类的决定
    Config appConfig;
    appConfig.discount = 0.05;
    std::cout << "实际价格 " << finalPrice(100, appConfig) << '\n';
}
