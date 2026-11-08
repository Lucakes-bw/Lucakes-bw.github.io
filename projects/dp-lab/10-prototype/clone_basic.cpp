// 《原型》：让对象自己复制自己——虚函数 clone()
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

std::string num(double v) { std::ostringstream os; os << v; return os.str(); }

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;    // 「复制我自己」
    virtual std::string describe() const = 0;
};

struct Circle : Shape {
    double r = 1;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Circle>(*this); }   // 调用自己的拷贝构造函数
    std::string describe() const override { return "半径 " + num(r) + " 的圆"; }
};

struct Text : Shape {
    std::string content = "Hello";
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Text>(*this); }
    std::string describe() const override { return "文字「" + content + "」"; }
};

int main() {
    std::vector<std::unique_ptr<Shape>> canvas;
    auto c = std::make_unique<Circle>(); c->r = 2.5;
    canvas.push_back(std::move(c));
    canvas.push_back(std::make_unique<Text>());

    std::vector<std::unique_ptr<Shape>> clipboard;
    for (const auto& s : canvas) clipboard.push_back(s->clone());   // 不用知道具体类型
    for (const auto& s : clipboard) std::cout << "复制出：" << s->describe() << '\n';
}
