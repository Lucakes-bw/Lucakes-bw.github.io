// 《原型》：用 CRTP 自动生成 clone()，再也不会忘记
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual std::string describe() const = 0;
};

// Cloneable<Derived, Base>：继承 Base，并替 Derived 实现 clone()
template <typename Derived, typename Base = Shape>
struct Cloneable : Base {
    using Base::Base;
    std::unique_ptr<Shape> clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));   // 按真实类型拷贝
    }
};

struct Circle : Cloneable<Circle> {
    double r = 1;
    std::string describe() const override { return "圆"; }
};

struct FilledCircle : Cloneable<FilledCircle, Circle> {     // 只要按这个格式写继承，clone 就自动是对的
    std::string color = "红色";
    std::string describe() const override { return color + "填充的圆"; }
};

int main() {
    FilledCircle original;
    original.color = "蓝色";
    std::unique_ptr<Shape> copy = original.clone();
    std::cout << "原件：" << original.describe() << '\n';
    std::cout << "副本：" << copy->describe() << '\n';
    const Shape& c = *copy;
    std::cout << "类型相同吗？" << (typeid(c) == typeid(original) ? "是" : "不是") << '\n';
}
