// 《UML 类图》第 3.7 节：组合、聚合、关联三种关系下，部分对象的生命周期
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Engine {                       // 组合的部分
    Engine()  { std::cout << "  Engine 构造\n"; }
    ~Engine() { std::cout << "  Engine 析构\n"; }
};

struct Wheel {                        // 聚合的部分
    explicit Wheel(int id) : id(id) { std::cout << "  Wheel" << id << " 构造\n"; }
    ~Wheel() { std::cout << "  Wheel" << id << " 析构\n"; }
    int id;
};

struct Driver {                       // 关联的对象
    std::string name;
};

class Car {
public:
    explicit Car(std::vector<std::shared_ptr<Wheel>> wheels) : wheels_(std::move(wheels)) {
        std::cout << "  Car 构造\n";
    }
    ~Car() { std::cout << "  Car 析构\n"; }
    void setDriver(Driver* d) { driver_ = d; }

private:
    Engine engine_;                                // 组合：值成员，和 Car 同生共死
    std::vector<std::shared_ptr<Wheel>> wheels_;   // 聚合：和外面共享，Car 没了轮子还在
    Driver* driver_ = nullptr;                     // 关联：只是认识，不管它的死活
};

int main() {
    auto w1 = std::make_shared<Wheel>(1);
    auto w2 = std::make_shared<Wheel>(2);
    Driver tom{"Tom"};

    std::cout << "进入作用域\n";
    {
        Car car({w1, w2});
        car.setDriver(&tom);
        std::cout << "  车里的 Wheel1 有 " << w1.use_count() << " 个主人\n";
    }
    std::cout << "离开作用域\n";
    std::cout << "  Wheel1 还剩 " << w1.use_count() << " 个主人\n";
    std::cout << "  司机 " << tom.name << " 还在\n";
    std::cout << "main 结束\n";
}
