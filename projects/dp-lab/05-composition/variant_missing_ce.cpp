// 《组合优于继承》练习 3：variant 加了 Tri，访问时漏写了它的分支（故意编译不过）
#include <iostream>
#include <variant>
#include <vector>

struct Circle { double r; };
struct Rect { double w, h; };
struct Tri { double b, h; };                         // 新加的类型
using AnyShape = std::variant<Circle, Rect, Tri>;

template <typename... Fs> struct overloaded : Fs... { using Fs::operator()...; };
template <typename... Fs> overloaded(Fs...) -> overloaded<Fs...>;

double area(const AnyShape& s) {
    return std::visit(overloaded{
        [](const Circle& c) { return 3.14159 * c.r * c.r; },
        [](const Rect& r) { return r.w * r.h; },
        // 忘了写 Tri
    }, s);
}

int main() {
    std::vector<AnyShape> v = {Circle{1}, Rect{2, 3}, Tri{2, 2}};
    double sum = 0;
    for (const auto& s : v) sum += area(s);
    std::cout << sum << '\n';
}
