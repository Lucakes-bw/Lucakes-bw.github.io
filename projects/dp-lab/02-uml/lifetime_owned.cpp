// 《UML 类图》练习 4：轮子改为 Car 自己创建的 unique_ptr，聚合变成组合
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Engine {
    Engine()  { std::cout << "  Engine 构造\n"; }
    ~Engine() { std::cout << "  Engine 析构\n"; }
};

struct Wheel {
    explicit Wheel(int id) : id(id) { std::cout << "  Wheel" << id << " 构造\n"; }
    ~Wheel() { std::cout << "  Wheel" << id << " 析构\n"; }
    int id;
};

class Car {
public:
    Car() {
        wheels_.push_back(std::make_unique<Wheel>(1));
        wheels_.push_back(std::make_unique<Wheel>(2));
        std::cout << "  Car 构造\n";
    }
    ~Car() { std::cout << "  Car 析构\n"; }

private:
    Engine engine_;                                // 组合
    std::vector<std::unique_ptr<Wheel>> wheels_;   // 组合：Car 自己创建、独占
};

int main() {
    std::cout << "进入作用域\n";
    {
        Car car;
    }
    std::cout << "离开作用域\n";
    std::cout << "main 结束\n";
}
