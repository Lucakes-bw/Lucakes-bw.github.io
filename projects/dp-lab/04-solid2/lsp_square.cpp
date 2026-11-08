// 《SOLID 原则（下）》：正方形「是一种」长方形，可是把它当长方形用就出错
#include <iostream>

class Rectangle {
public:
    virtual ~Rectangle() = default;
    virtual void setWidth(double w) { w_ = w; }
    virtual void setHeight(double h) { h_ = h; }
    double width() const { return w_; }
    double height() const { return h_; }
    double area() const { return w_ * h_; }

protected:
    double w_ = 0, h_ = 0;
};

class Square : public Rectangle {          // 正方形：宽和高必须相等
public:
    void setWidth(double w) override { w_ = h_ = w; }
    void setHeight(double h) override { w_ = h_ = h; }
};

// 使用长方形的代码：把一块区域调整成横幅广告的尺寸 728 × 90
void fitToBanner(Rectangle& r) {
    r.setWidth(728);
    r.setHeight(90);
}

// 契约测试：对每一个「长方形」都应该成立的约定
bool checkRectangleContract(Rectangle& r, const char* name) {
    r.setWidth(5);
    r.setHeight(4);
    bool ok = r.width() == 5 && r.height() == 4 && r.area() == 20;
    std::cout << name << "：设置宽 5、高 4 后，宽 " << r.width() << "，高 " << r.height()
              << "，面积 " << r.area() << (ok ? "  ✓" : "  ✗ 违反了长方形的约定") << '\n';
    return ok;
}

int main() {
    Rectangle rect;
    Square square;
    checkRectangleContract(rect, "Rectangle");
    checkRectangleContract(square, "Square");

    fitToBanner(rect);
    fitToBanner(square);
    std::cout << "横幅：Rectangle " << rect.width() << " × " << rect.height()
              << "，Square " << square.width() << " × " << square.height() << '\n';
}
