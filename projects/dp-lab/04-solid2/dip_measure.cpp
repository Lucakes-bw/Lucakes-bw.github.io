// 《SOLID 原则（下）》：同一组 1000 个下单测试，分别用写文件的实现和内存实现，比较耗时
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

struct Order { std::string customer; int qty; double price; };

class OrderStore {
public:
    virtual ~OrderStore() = default;
    virtual void save(const Order& o) = 0;
};

class FileOrderStore : public OrderStore {
public:
    explicit FileOrderStore(std::string path) : path_(std::move(path)) {}
    void save(const Order& o) override {
        FILE* f = std::fopen(path_.c_str(), "a");
        std::fprintf(f, "%s,%d,%.2f\n", o.customer.c_str(), o.qty, o.price);
        std::fflush(f);
        fsync(fileno(f));
        std::fclose(f);
    }
private:
    std::string path_;
};

class InMemoryOrderStore : public OrderStore {
public:
    void save(const Order& o) override { orders.push_back(o); }
    std::vector<Order> orders;
};

class OrderService {
public:
    explicit OrderService(OrderStore& store) : store_(store) {}
    bool place(const Order& o) {
        if (o.qty <= 0 || o.qty > 100 || o.price <= 0) return false;
        store_.save(o);
        return true;
    }
private:
    OrderStore& store_;
};

double runTests(OrderStore& store) {
    OrderService svc(store);
    auto t0 = std::chrono::steady_clock::now();
    int passed = 0;
    for (int i = 0; i < 1000; ++i) {
        int qty = i % 120 - 5;                  // 有合法的，也有不合法的
        bool expected = qty > 0 && qty <= 100;
        passed += svc.place({"c" + std::to_string(i), qty, 9.9}) == expected;
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (passed != 1000) std::cout << "有测试失败\n";
    return ms;
}

int main() {
    std::remove("orders_test.csv");
    FileOrderStore file("orders_test.csv");
    InMemoryOrderStore mem;
    std::printf("写文件（fsync）：%8.2f ms\n", runTests(file));
    std::printf("内存实现：      %8.3f ms\n", runTests(mem));
    std::remove("orders_test.csv");
}
