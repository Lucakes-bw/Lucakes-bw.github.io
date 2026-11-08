// 《SOLID 原则（上）》：一个类服务三个部门，财务改了共用的函数，人事的报表跟着变了
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Employee {
    std::string name;
    std::vector<double> dailyHours;   // 这周每天的工时
    double hourlyRate;
};

// 财务部要它算工资，人事部要它出工时报表，运维要它存数据库
class EmployeeService {
public:
    double calculatePay(const Employee& e) const {              // 财务部
        double regular = regularHours(e);
        double overtime = totalHours(e) - regular;
        return regular * e.hourlyRate + overtime * e.hourlyRate * 1.5;
    }

    void reportHours(const Employee& e) const {                 // 人事部
        double regular = regularHours(e);
        std::cout << e.name << " 正常工时 " << regular << " 小时，加班 " << totalHours(e) - regular << " 小时\n";
    }

    void save(const Employee& e) const {                        // 运维
        std::cout << "INSERT INTO employee VALUES ('" << e.name << "', " << e.hourlyRate << ")\n";
    }

private:
    // 两个部门共用：每天超过上限的部分算加班
    double regularHours(const Employee& e) const {
        const double limit = 9;       // 财务部要求：前 9 小时都按正常工资发（原来是 8）
        double sum = 0;
        for (double h : e.dailyHours) sum += std::min(h, limit);
        return sum;
    }
    double totalHours(const Employee& e) const {
        double sum = 0;
        for (double h : e.dailyHours) sum += h;
        return sum;
    }
};

int main() {
    Employee alice{"Alice", {8, 10, 9, 8, 11}, 100};
    EmployeeService svc;
    std::cout << "工资：" << svc.calculatePay(alice) << '\n';
    svc.reportHours(alice);
}
