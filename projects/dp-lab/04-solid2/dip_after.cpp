// 《SOLID 原则（下）》：业务逻辑只依赖自己定义的接口，测试时换成内存里的实现
#include <iostream>
#include <string>
#include <vector>

struct Order {
    std::string customer;
    int qty;
    double price;
};

// 接口由高层（业务）定义：业务需要什么，就规定什么
class OrderStore {
public:
    virtual ~OrderStore() = default;
    virtual void save(const Order& o) = 0;
};

class OrderService {
public:
    explicit OrderService(OrderStore& store) : store_(store) {}   // 依赖从外面注入
    bool place(const Order& o) {
        if (o.qty <= 0 || o.qty > 100 || o.price <= 0) return false;
        store_.save(o);
        return true;
    }
private:
    OrderStore& store_;
};

// 测试用的实现：订单存在内存里，测试结束就没了，测试之间互不影响
class InMemoryOrderStore : public OrderStore {
public:
    void save(const Order& o) override { orders.push_back(o); }
    std::vector<Order> orders;
};

int main() {
    InMemoryOrderStore store;
    OrderService svc(store);
    int failed = 0;
    auto expect = [&](bool cond, const char* what) { if (!cond) { ++failed; std::cout << "✗ " << what << '\n'; } };

    expect(svc.place({"alice", 2, 99.5}), "正常订单应该成功");
    expect(!svc.place({"bob", 0, 10}), "数量为 0 应该失败");
    expect(!svc.place({"carol", 101, 10}), "数量超过 100 应该失败");
    expect(!svc.place({"dave", 1, -5}), "价格为负应该失败");
    expect(store.orders.size() == 1, "只有一个订单被保存");
    std::cout << (failed ? "有测试失败" : "5 个测试全部通过") << "，保存了 " << store.orders.size() << " 个订单\n";
}
