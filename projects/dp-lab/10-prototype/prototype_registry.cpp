// 《原型》：原型注册表——工具栏上的每种图形都是一个原型，拖到画布上就是克隆一份再修改
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

std::string num(double v) { std::ostringstream os; os << v; return os.str(); }

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual std::string describe() const = 0;
    double x = 0, y = 0;
};

template <typename Derived>
struct Cloneable : Shape {
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Derived>(static_cast<const Derived&>(*this)); }
};

struct Circle : Cloneable<Circle> {
    double r = 10;
    std::string color = "黑色";
    std::string describe() const override { return color + "的圆，半径 " + num(r) + "，位于 (" + num(x) + ", " + num(y) + ")"; }
};

struct Label : Cloneable<Label> {
    std::string text, font = "宋体";
    int size = 12;
    std::string describe() const override { return font + " " + std::to_string(size) + " 号字「" + text + "」，位于 (" + num(x) + ", " + num(y) + ")"; }
};

class Palette {                                     // 原型注册表
public:
    void add(const std::string& name, std::unique_ptr<Shape> proto) { protos_[name] = std::move(proto); }
    std::unique_ptr<Shape> create(const std::string& name, double x, double y) const {
        auto it = protos_.find(name);
        if (it == protos_.end()) return nullptr;
        auto s = it->second->clone();               // 克隆原型
        s->x = x; s->y = y;                         // 只改需要改的
        return s;
    }
private:
    std::map<std::string, std::unique_ptr<Shape>> protos_;
};

int main() {
    Palette palette;
    auto red = std::make_unique<Circle>(); red->color = "红色"; red->r = 20;
    palette.add("红色大圆", std::move(red));
    auto title = std::make_unique<Label>(); title->text = "标题"; title->font = "黑体"; title->size = 32;
    palette.add("标题文字", std::move(title));

    std::vector<std::unique_ptr<Shape>> canvas;
    canvas.push_back(palette.create("红色大圆", 100, 50));
    canvas.push_back(palette.create("红色大圆", 300, 50));
    canvas.push_back(palette.create("标题文字", 120, 10));
    for (const auto& s : canvas) std::cout << s->describe() << '\n';
}
