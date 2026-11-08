// 《SOLID 原则（上）》：按「谁会要求修改」拆成三个类，各自的规则互不影响
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Employee {                     // 只放数据，不放任何部门的规则
    std::string name;
    std::vector<double> dailyHours;
    double hourlyRate;
};

double totalHours(const Employee& e) {
    double sum = 0;
    for (double h : e.dailyHours) sum += h;
    return sum;
}

double hoursWithin(const Employee& e, double limit) {   // 纯粹的计算工具，不含任何部门的规则
    double sum = 0;
    for (double h : e.dailyHours) sum += std::min(h, limit);
    return sum;
}

class PayCalculator {                 // 只对财务部负责
public:
    double pay(const Employee& e) const {
        double regular = hoursWithin(e, kPaidRegularLimit);
        return regular * e.hourlyRate + (totalHours(e) - regular) * e.hourlyRate * 1.5;
    }
private:
    static constexpr double kPaidRegularLimit = 9;      // 公司的发薪规则
};

class HourReporter {                  // 只对人事部负责
public:
    void report(const Employee& e) const {
        double regular = hoursWithin(e, kLegalRegularLimit);
        std::cout << e.name << " 正常工时 " << regular << " 小时，加班 " << totalHours(e) - regular << " 小时\n";
    }
private:
    static constexpr double kLegalRegularLimit = 8;     // 劳动法规定的标准工时
};

class EmployeeRepository {            // 只对运维负责
public:
    void save(const Employee& e) const {
        std::cout << "INSERT INTO employee VALUES ('" << e.name << "', " << e.hourlyRate << ")\n";
    }
};

int main() {
    Employee alice{"Alice", {8, 10, 9, 8, 11}, 100};
    std::cout << "工资：" << PayCalculator().pay(alice) << '\n';
    HourReporter().report(alice);
}
