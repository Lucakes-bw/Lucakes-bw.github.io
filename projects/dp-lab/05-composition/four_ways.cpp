// 《组合优于继承》：同一件事（求一组形状的总面积），C++ 里的四种多态写法
#include <functional>
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

// ---------- ① 虚函数：继承一个接口 ----------
struct Shape {
    virtual ~Shape() = default;
    virtual double area() const = 0;
};
struct VCircle : Shape { double r; explicit VCircle(double r) : r(r) {} double area() const override { return 3.14159 * r * r; } };
struct VRect : Shape { double w, h; VRect(double w, double h) : w(w), h(h) {} double area() const override { return w * h; } };

double totalVirtual(const std::vector<std::unique_ptr<Shape>>& v) {
    double sum = 0;
    for (const auto& s : v) sum += s->area();
    return sum;
}

// ---------- 下面三种用的是普通的类，没有基类，也没有虚函数 ----------
struct Circle { double r; double area() const { return 3.14159 * r * r; } };
struct Rect { double w, h; double area() const { return w * h; } };

// ---------- ② std::function：只要「能调用、返回 double」就行 ----------
double totalFunction(const std::vector<std::function<double()>>& v) {
    double sum = 0;
    for (const auto& f : v) sum += f();
    return sum;
}

// ---------- ③ 模板：编译期确定类型，每种类型生成一份代码 ----------
template <typename T>
double totalOf(const std::vector<T>& v) {
    double sum = 0;
    for (const auto& s : v) sum += s.area();
    return sum;
}

// ---------- ④ std::variant：类型的集合是固定的 ----------
using AnyShape = std::variant<Circle, Rect>;

double totalVariant(const std::vector<AnyShape>& v) {
    double sum = 0;
    for (const auto& s : v) sum += std::visit([](const auto& x) { return x.area(); }, s);
    return sum;
}

int main() {
    std::vector<std::unique_ptr<Shape>> v1;
    v1.push_back(std::make_unique<VCircle>(1));
    v1.push_back(std::make_unique<VRect>(2, 3));

    Circle c{1};
    Rect r{2, 3};
    std::vector<std::function<double()>> v2 = {[c] { return c.area(); }, [r] { return r.area(); }};

    std::vector<Circle> circles = {c};
    std::vector<Rect> rects = {r};

    std::vector<AnyShape> v4 = {c, r};

    std::cout << "① 虚函数       " << totalVirtual(v1) << '\n';
    std::cout << "② std::function " << totalFunction(v2) << '\n';
    std::cout << "③ 模板         " << totalOf(circles) + totalOf(rects) << '\n';
    std::cout << "④ std::variant  " << totalVariant(v4) << '\n';
}
