// 《SOLID 原则（下）》：业务逻辑直接依赖写文件的存储类，想测下单规则就得真的写文件
#include <cstdio>
#include <iostream>
#include <string>
#include <unistd.h>

struct Order {
    std::string customer;
    int qty;
    double price;
};

class FileOrderStore {                         // 低层：每个订单追加一行，并确保真正写到磁盘上
public:
    explicit FileOrderStore(std::string path) : path_(std::move(path)) {}
    void save(const Order& o) {
        FILE* f = std::fopen(path_.c_str(), "a");
        std::fprintf(f, "%s,%d,%.2f\n", o.customer.c_str(), o.qty, o.price);
        std::fflush(f);
        fsync(fileno(f));                       // 数据库也会这样做：断电也不能丢订单
        std::fclose(f);
    }
private:
    std::string path_;
};

class OrderService {                           // 高层：下单的业务规则
public:
    bool place(const Order& o) {
        if (o.qty <= 0 || o.qty > 100 || o.price <= 0) return false;   // 业务规则
        store_.save(o);
        return true;
    }
private:
    FileOrderStore store_{"orders.csv"};       // 直接创建具体的存储类：换不掉
};

int main() {
    OrderService svc;
    std::cout << svc.place({"alice", 2, 99.5}) << svc.place({"bob", 0, 10}) << '\n';
}
