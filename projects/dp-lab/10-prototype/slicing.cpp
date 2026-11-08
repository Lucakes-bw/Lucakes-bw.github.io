// 《原型》：手里只有基类指针，怎么复制出一个同样的对象？
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

std::string num(double v) { std::ostringstream os; os << v; return os.str(); }

struct Shape {
    virtual ~Shape() = default;
    virtual std::string describe() const { return "一个形状"; }
};
struct Circle : Shape {
    double r = 1;
    std::string describe() const override { return "半径 " + num(r) + " 的圆"; }
};
struct Text : Shape {
    std::string content = "Hello";
    std::string describe() const override { return "文字「" + content + "」"; }
};

int main() {
    std::vector<std::unique_ptr<Shape>> canvas;
    auto c = std::make_unique<Circle>(); c->r = 2.5;
    canvas.push_back(std::move(c));
    canvas.push_back(std::make_unique<Text>());

    // 用户选中所有图形，按下「复制」
    // 尝试一：按值拷贝基类——发生对象切片，只复制了 Shape 那一部分
    for (const auto& s : canvas) {
        Shape copy = *s;
        std::cout << "按值拷贝：" << copy.describe() << '\n';
    }

    // 尝试二：逐个判断具体类型——能用，但每加一种图形都要回来改这里
    for (const auto& s : canvas) {
        std::unique_ptr<Shape> copy;
        if (auto* p = dynamic_cast<Circle*>(s.get())) copy = std::make_unique<Circle>(*p);
        else if (auto* p = dynamic_cast<Text*>(s.get())) copy = std::make_unique<Text>(*p);
        std::cout << "dynamic_cast：" << (copy ? copy->describe() : "不认识的类型") << '\n';
    }
}
