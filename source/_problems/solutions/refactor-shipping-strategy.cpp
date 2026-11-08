#include <map>
#include <memory>
#include <string>
using namespace std;

// 策略接口：每种快递方式怎么算运费
class ShippingStrategy {
public:
    virtual ~ShippingStrategy() = default;
    virtual double cost(double weight, int distance) const = 0;
};

class Standard : public ShippingStrategy {
public:
    double cost(double weight, int distance) const override {
        return 5 + weight * 2 + (distance > 500 ? 10 : 0);
    }
};

class Express : public ShippingStrategy {
public:
    double cost(double weight, int distance) const override {
        double c = 12 + weight * 3.5;
        return distance > 500 ? c * 1.5 : c;
    }
};

class Economy : public ShippingStrategy {
public:
    double cost(double weight, int) const override {
        return weight <= 1 ? 3 : 3 + (weight - 1) * 1.2;
    }
};

class Drone : public ShippingStrategy {
public:
    double cost(double weight, int distance) const override {
        if (weight > 5 || distance > 20) return -1;
        return 20 + 1.5 * distance;
    }
};

class Solution {
public:
    Solution() {
        strategies_["standard"] = make_unique<Standard>();
        strategies_["express"] = make_unique<Express>();
        strategies_["economy"] = make_unique<Economy>();
        strategies_["drone"] = make_unique<Drone>();     // 新需求：只加了这一行和上面的 Drone 类
    }

    double shippingCost(string method, double weight, int distance) {
        auto it = strategies_.find(method);
        return it == strategies_.end() ? -1 : it->second->cost(weight, distance);
    }

private:
    map<string, unique_ptr<ShippingStrategy>> strategies_;
};
