// 《SOLID 原则（下）》：不让 Square 继承 Rectangle，它们只共享真正共同的行为
#include <iostream>
#include <memory>
#include <vector>

class Shape {                              // 所有形状都能做的事：求面积
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
};

class Rectangle : public Shape {
public:
    Rectangle(double w, double h) : w_(w), h_(h) {}
    double area() const override { return w_ * h_; }
    Rectangle withSize(double w, double h) const { return {w, h}; }   // 改尺寸得到一个新对象
private:
    double w_, h_;
};

class Square : public Shape {
public:
    explicit Square(double side) : side_(side) {}
    double area() const override { return side_ * side_; }
    Square withSide(double s) const { return Square(s); }
private:
    double side_;
};

int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Rectangle>(5, 4));
    shapes.push_back(std::make_unique<Square>(4));
    double total = 0;
    for (const auto& s : shapes) total += s->area();      // 任何 Shape 都可以放心地替换进来
    std::cout << "总面积 " << total << '\n';
    // fitToBanner 只接受 Rectangle：编译器不会让你把 Square 传进去
    Rectangle banner = Rectangle(1, 1).withSize(728, 90);
    std::cout << "横幅面积 " << banner.area() << '\n';
}
