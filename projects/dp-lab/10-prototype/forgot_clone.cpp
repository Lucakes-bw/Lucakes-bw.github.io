// 《原型》：孙类忘了重写 clone()，复制出来的对象悄悄丢了一部分
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual std::string describe() const = 0;
};

struct Circle : Shape {
    double r = 1;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Circle>(*this); }
    std::string describe() const override { return "圆"; }
};

// 后来有人加了带填充色的圆，继承 Circle，重写了 describe，却忘了重写 clone
struct FilledCircle : Circle {
    std::string color = "红色";
    std::string describe() const override { return color + "填充的圆"; }
};

int main() {
    FilledCircle original;
    original.color = "蓝色";
    std::unique_ptr<Shape> copy = original.clone();   // 调用的是 Circle::clone
    std::cout << "原件：" << original.describe() << '\n';
    std::cout << "副本：" << copy->describe() << '\n';
    const Shape& c = *copy;
    std::cout << "类型相同吗？" << (typeid(c) == typeid(original) ? "是" : "不是") << '\n';
}
