---
title: SOLID 原则（上）：单一职责与开闭
date: 2026-11-08 11:00:00
updated: 2026-11-08 11:00:00
categories:
  - 设计模式
  - 基础与设计原则
tags:
  - 设计模式
  - SOLID
  - 单一职责
  - 开闭原则
order: 3
description: 单一职责不是「一个类只做一件事」，而是「只对一类角色负责」。实测一个类同时服务财务和人事：财务改了共用的工时函数，人事报表里的加班就从 6 小时悄悄变成了 3 小时；一个大而全的头文件，让只用到求平均值的源文件在 GCC 上多花 6 倍编译时间。开闭原则让新需求只需新增代码：密码规则变成对象，加规则、换产品线都不用改 validate；做到极致是插件，新增一条规则后主程序的 md5 一个字节都没变。
top_group_list: true
disableNunjucks: true
viz: [dp-ocp]
---

> **本文目标**：理解单一职责原则的真正含义（按「谁会要求修改」划分）；知道职责混在一起在 C++ 里带来的两种代价：改一处坏另一处，以及多余的编译依赖；理解开闭原则，会用多态、`std::function` 和插件三种方式留出扩展点；知道什么时候不该为了这两条原则而拆分代码。
>
> **前置知识**：《为什么需要设计模式》《UML 类图》；C/C++ 专栏的《多态与虚函数》《Lambda 表达式》《std::function 与 std::bind》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/03-solid1/` 目录。

## 1. SOLID 是什么

SOLID 是五条面向对象设计原则的首字母缩写，由 Robert C. Martin 在 2000 年前后整理提出（缩写本身是 Michael Feathers 起的）。上一篇讲到，设计模式的共同出发点是「把会变化的部分隔离开，让新需求只需要新增代码」；SOLID 就是这句话展开后的五条具体准则，后面每个模式几乎都能对应到其中的一两条。

<figure class="diagram">
<svg viewBox="0 0 720 170" role="img" aria-label="SOLID 五条原则：单一职责、开闭、里氏替换、接口隔离、依赖倒置；本文讲前两条"><g style="font-family:var(--mono)"><rect x="10" y="10" width="132" height="130" rx="8" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:2"/><text x="76.0" y="52" text-anchor="middle" style="fill:var(--accent);font-size:34px;font-weight:700">S</text><text x="76.0" y="78" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">单一职责</text><text x="76.0" y="96" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Single Responsibility</text><text x="76.0" y="124" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">一个模块只对一类角色负责</text><rect x="152" y="10" width="132" height="130" rx="8" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:2"/><text x="218.0" y="52" text-anchor="middle" style="fill:var(--accent);font-size:34px;font-weight:700">O</text><text x="218.0" y="78" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">开闭</text><text x="218.0" y="96" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Open / Closed</text><text x="218.0" y="124" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">对扩展开放，对修改关闭</text><rect x="294" y="10" width="132" height="130" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="360.0" y="52" text-anchor="middle" style="fill:var(--muted);font-size:34px;font-weight:700">L</text><text x="360.0" y="78" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">里氏替换</text><text x="360.0" y="96" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Liskov Substitution</text><text x="360.0" y="124" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">子类能替换父类而不出错</text><rect x="436" y="10" width="132" height="130" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="502.0" y="52" text-anchor="middle" style="fill:var(--muted);font-size:34px;font-weight:700">I</text><text x="502.0" y="78" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">接口隔离</text><text x="502.0" y="96" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Interface Segregation</text><text x="502.0" y="124" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">不强迫依赖用不到的接口</text><rect x="578" y="10" width="132" height="130" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="644.0" y="52" text-anchor="middle" style="fill:var(--muted);font-size:34px;font-weight:700">D</text><text x="644.0" y="78" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">依赖倒置</text><text x="644.0" y="96" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Dependency Inversion</text><text x="644.0" y="124" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">高层和低层都依赖抽象</text><text x="147.0" y="160" text-anchor="middle" style="fill:var(--accent);font-size:12px;font-weight:700">本文（上）</text><text x="502.0" y="160" text-anchor="middle" style="fill:var(--muted);font-size:12px">下一篇（下）</text></g></svg>
</figure>

本文讲前两条：**单一职责**回答「代码该怎么切分」，**开闭**回答「切开以后，新需求该落在哪里」。后三条在下一篇。

## 2. 单一职责原则（SRP）

### 2.1 不是「只做一件事」

单一职责原则（Single Responsibility Principle）最常见的误解是「一个类只做一件事」。按这个理解，一个类只能有一个函数，显然不对。

Martin 最初的表述是：**一个类应该只有一个引起它变化的原因**。后来他在《架构整洁之道》里说得更直白：**一个模块应该只对一类角色（actor）负责**。这里的「角色」是会提出修改要求的人或部门：财务、人事、运维、产品经理……

所以判断一个类是否符合单一职责，不是数它有几个函数，而是问：**哪些人会要求修改这个类？** 如果答案超过一类，它就承担了多个职责。

### 2.2 实测：财务改了一处，人事的报表错了

下面的 `EmployeeService` 同时服务三个部门：财务部用 `calculatePay` 算工资，人事部用 `reportHours` 出工时报表，运维用 `save` 存数据库。两个部门都要区分「正常工时」和「加班」，于是写了一个共用的 `regularHours`，避免重复代码，看起来是好习惯。

```uml
label: EmployeeService 同时服务三个部门：财务部调用 calculatePay，人事部调用 reportHours，运维调用 save；calculatePay 和 reportHours 共用私有函数 regularHours
classes:
  - { id: Svc, name: EmployeeService, x: 230, y: 16, w: 270, ops: ['+ calculatePay(e) : double', '+ reportHours(e)', '+ save(e)', '- regularHours(e) : double  // 共用'] }
notes:
  - { x: 20, y: 22, text: '财务部：算工资', to: Svc }
  - { x: 20, y: 72, text: '人事部：工时报表', to: Svc }
  - { x: 20, y: 122, text: '运维：存数据库', to: Svc }
```

某天财务部提出：按公司新规，每天前 9 小时都按正常工资发（原来是前 8 小时），超过 9 小时的部分才按 1.5 倍算加班费。开发者找到 `regularHours`，把上限从 8 改成 9，财务部验收通过：

```cpp
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
```

改动前（上限 8）和改动后（上限 9）的输出：

```text
改动前：工资：4900    Alice 正常工时 40 小时，加班 6 小时
改动后：工资：4750    Alice 正常工时 43 小时，加班 3 小时
```

工资变了，这是财务要的。可人事报表里的加班也从 6 小时变成了 3 小时，这不是任何人想要的：劳动法规定的标准工时仍然是每天 8 小时，人事部要按这个口径上报加班。财务部的需求，悄悄改坏了人事部的报表，而且两边各自的测试很可能都发现不了，因为财务的测试只看工资，人事那边没人知道这次改动。

问题的根源是：**`regularHours` 看起来是同一段代码，其实代表两条不同的业务规则**（公司的发薪规则、劳动法的工时口径）。它们碰巧一样，但会因为不同的人、在不同的时间变化。把它们合并成一个函数，就把两个部门绑在了一起。

### 2.3 按角色拆开

按「谁会要求修改」拆成三个类，各自持有自己的规则；`Employee` 只放数据，共用的只有不含任何业务规则的计算工具：

```uml
label: 拆分后：PayCalculator 只对财务部负责，HourReporter 只对人事部负责，EmployeeRepository 只对运维负责；三者都只依赖只含数据的 Employee
classes:
  - { id: Pay, name: PayCalculator, x: 20, y: 20, w: 200, attrs: ['$- kPaidRegularLimit = 9'], ops: ['+ pay(e) : double'] }
  - { id: Hour, name: HourReporter, x: 250, y: 20, w: 200, attrs: ['$- kLegalRegularLimit = 8'], ops: ['+ report(e)'] }
  - { id: Repo, name: EmployeeRepository, x: 480, y: 20, w: 190, ops: ['+ save(e)'] }
  - { id: Emp, name: Employee, x: 239, y: 200, w: 200, attrs: ['+ name : string', '+ dailyHours : vector<double>', '+ hourlyRate : double'] }
rels:
  - { from: Pay, to: Emp, type: depend, mid: 160 }
  - { from: Hour, to: Emp, type: depend }
  - { from: Repo, to: Emp, type: depend, mid: 160 }
texts:
  - { x: 120, y: 14, text: '财务部', anchor: middle, muted: true }
  - { x: 350, y: 14, text: '人事部', anchor: middle, muted: true }
  - { x: 575, y: 14, text: '运维', anchor: middle, muted: true }
```

```cpp
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
```

```text
工资：4750
Alice 正常工时 40 小时，加班 6 小时
```

现在财务再改发薪规则，只会改 `PayCalculator`；人事报表的口径写在 `HourReporter` 里，谁也碰不到谁。注意 `hoursWithin(e, limit)` 仍然是共用的，但它只是一个「把每天的工时截断到上限再求和」的计算工具，不包含「上限是多少」这条业务规则，所以共用它是安全的。

> 「不要重复」（DRY）说的是**同一份知识**不要写两遍，而不是**长得一样的代码**不要写两遍。两段代码长得一样、但代表不同的知识时，合并它们反而是错的。

### 2.4 C++ 里的另一种代价：编译依赖

职责混在一起，在 C++ 里还有一个看得见的代价：**头文件依赖**。假设报表相关的功能都写在一个头文件里，解析、校验、统计、格式化、保存样样俱全（`headers/report_all.h`）：

<!-- run: off -->
```cpp
#pragma once
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

class ReportService {
public:
    std::vector<double> parse(const std::string& text) const;           // 用到 <sstream>
    bool validate(const std::string& line) const;                       // 用到 <regex>
    double average(const std::vector<double>& v) const;                 // 用到 <numeric>
    std::string toJson(const std::map<std::string, double>& m) const;   // 用到 <iomanip> <map>
    void save(const std::string& path, const std::string& content) const;   // 用到 <fstream>
    // ……函数体都直接写在头文件里
};
```

另一个源文件只想求个平均值，也只能包含整个 `report_all.h`。把「统计」单独拆成 `stats.h`（只包含 `<numeric>` 和 `<vector>`）以后，同样一个只调用 `average()` 的源文件，编译代价差了多少？我用 `headers/measure.py` 各编译 9 次取中位数（Apple M6；GCC 运行在同一台机器上的 Ubuntu 24.04 容器里）：

| 编译器 | 包含 | 预处理后的行数 | 编译时间 |
| --- | --- | --- | --- |
| Clang 21 + libc++ | `report_all.h` | 77959 | 246 ms |
| Clang 21 + libc++ | `stats.h` | 59406 | 146 ms |
| GCC 13 + libstdc++ | `report_all.h` | 74911 | 192 ms |
| GCC 13 + libstdc++ | `stats.h` | 20681 | 31 ms |

<figure class="diagram">
<svg viewBox="0 0 760 250" role="img" aria-label="只用到求平均值的源文件：包含大而全的 report_all.h 时，Clang 编译 246 毫秒、GCC 192 毫秒；只包含 stats.h 时，Clang 146 毫秒、GCC 31 毫秒"><g style="font-family:var(--mono)"><text x="20" y="22" text-anchor="start" style="fill:var(--muted);font-size:11px">编译一个只用到 average() 的源文件（-O0，9 次取中位数）</text><text x="20" y="84" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">Clang 21 + libc++</text><rect x="210" y="50" width="344" height="24" rx="3" style="fill:var(--warn)"/><text x="562.4" y="66" text-anchor="start" style="fill:var(--warn);font-size:13px;font-weight:700">246 ms</text><text x="624.4" y="66" text-anchor="start" style="fill:var(--muted);font-size:11px">预处理后 77959 行</text><text x="202" y="66" text-anchor="end" style="fill:var(--fg-2);font-size:10px">#include "report_all.h"</text><rect x="210" y="84" width="204" height="24" rx="3" style="fill:var(--accent)"/><text x="422.4" y="100" text-anchor="start" style="fill:var(--accent);font-size:13px;font-weight:700">146 ms</text><text x="484.4" y="100" text-anchor="start" style="fill:var(--muted);font-size:11px">预处理后 59406 行</text><text x="202" y="100" text-anchor="end" style="fill:var(--fg-2);font-size:10px">#include "stats.h"</text><text x="20" y="180" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">GCC 13 + libstdc++</text><rect x="210" y="146" width="269" height="24" rx="3" style="fill:var(--warn)"/><text x="486.79999999999995" y="162" text-anchor="start" style="fill:var(--warn);font-size:13px;font-weight:700">192 ms</text><text x="548.8" y="162" text-anchor="start" style="fill:var(--muted);font-size:11px">预处理后 74911 行</text><text x="202" y="162" text-anchor="end" style="fill:var(--fg-2);font-size:10px">#include "report_all.h"</text><rect x="210" y="180" width="43" height="24" rx="3" style="fill:var(--accent)"/><text x="261.4" y="196" text-anchor="start" style="fill:var(--accent);font-size:13px;font-weight:700">31 ms</text><text x="323.4" y="196" text-anchor="start" style="fill:var(--muted);font-size:11px">预处理后 20681 行</text><text x="202" y="196" text-anchor="end" style="fill:var(--fg-2);font-size:10px">#include "stats.h"</text><text x="20" y="240" text-anchor="start" style="fill:var(--muted);font-size:11px">差别全部来自用不到的 &lt;regex&gt; &lt;fstream&gt; &lt;sstream&gt; &lt;iomanip&gt; &lt;map&gt;：它们只为 parse / validate / toJson / save 服务</text></g></svg>
</figure>

GCC 上差了 6 倍。libc++ 的 `<vector>` 本身就比较重，所以 Clang 上差得少一些，但也有 1.7 倍。一个文件多一两百毫秒看不出来；一个项目有几百个源文件都包含这个头文件时，每次全量编译就要多等几十秒。

更麻烦的是**重新编译的范围**：`report_all.h` 里任何一处改动，哪怕只是改了 `toJson` 的输出格式，所有包含它的源文件都要重新编译，包括那些只用到 `average()` 的。职责拆开以后，改格式化只影响用到格式化的文件。后面要讲的 Pimpl 惯用法，就是专门解决这个问题的。

### 2.5 不要拆过头

单一职责不等于「越小越好」。把每个函数都拆成一个类，代码会散成一地碎片，读一个功能要在十几个文件之间跳来跳去。判断标准还是那个问题：**这几段代码会不会因为不同的人、在不同的时间变化？**

- 会：拆开。比如上面的发薪规则和工时口径。
- 不会，总是一起改：放在一起。比如 `PayCalculator` 里的「正常工资」和「加班费」两部分，它们都由财务部决定、一起变化，拆成两个类只会增加跳转。

## 3. 开闭原则（OCP）

### 3.1 对扩展开放，对修改关闭

开闭原则（Open/Closed Principle）最早由 Bertrand Meyer 在 1988 年提出：**软件实体应该对扩展开放，对修改关闭**。意思是：加新功能时，应该通过**新增代码**来实现，而不是**修改已经写好、测试过的代码**。

这正是上一篇得出的结论：把会变化的方向安排在「新增」上。单一职责帮你找到变化的原因、把代码切开；开闭原则要求在切开的地方留好扩展点，让下一次变化只需要往里面「插」新代码。

### 3.2 一个越加越长的参数列表

公司有三条产品线：官网、后台管理系统、内部工具，它们对密码的要求各不相同。最直接的写法是一个函数加几个开关参数：

```cpp
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> validate(const std::string& pwd, size_t minLen, bool needUpper, bool checkCommon) {
    std::vector<std::string> errors;
    if (pwd.size() < minLen) errors.push_back("至少 " + std::to_string(minLen) + " 个字符");
    if (std::none_of(pwd.begin(), pwd.end(), [](unsigned char c) { return std::isdigit(c); }))
        errors.push_back("至少包含一个数字");
    if (needUpper && std::none_of(pwd.begin(), pwd.end(), [](unsigned char c) { return std::isupper(c); }))
        errors.push_back("至少包含一个大写字母");
    if (checkCommon) {
        static const std::vector<std::string> common = {"password", "Passw0rd", "12345678", "qwerty123"};
        if (std::find(common.begin(), common.end(), pwd) != common.end()) errors.push_back("不能使用常见密码");
    }
    return errors;
}

int main() {
    // 每加一个产品线的特殊要求，validate 就多一个参数，所有调用的地方都要跟着改
    for (const auto& e : validate("Passw0rd", 8, true, true)) std::cout << "官网：" << e << '\n';
    for (const auto& e : validate("Passw0rd", 12, true, true)) std::cout << "后台：" << e << '\n';
    for (const auto& e : validate("abc", 6, false, false)) std::cout << "内部工具：" << e << '\n';
}
```

```text
官网：不能使用常见密码
后台：至少 12 个字符
后台：不能使用常见密码
内部工具：至少 6 个字符
内部工具：至少包含一个数字
```

每多一条规则，`validate` 就要多一个参数、多一个 `if`，所有调用它的地方都要跟着改。更糟的是调用处可读性很差：`validate("Passw0rd", 12, true, true)` 里的两个 `true` 分别是什么意思，不翻到函数定义根本看不出来。

### 3.3 把规则变成对象

把每条规则变成一个对象：一个提示语，加一个「检查是否通过」的函数。`validate` 只负责把密码依次交给每条规则，每条产品线自己挑一组规则：

```cpp
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct Rule {
    std::string message;                                  // 不通过时的提示
    std::function<bool(const std::string&)> ok;           // 检查是否通过
};

// 对修改关闭：以后不管加多少规则，这个函数都不用再改
std::vector<std::string> validate(const std::string& pwd, const std::vector<Rule>& rules) {
    std::vector<std::string> errors;
    for (const Rule& r : rules)
        if (!r.ok(pwd)) errors.push_back(r.message);
    return errors;
}

// 对扩展开放：每条规则是一个返回 Rule 的函数，新规则就是新函数
Rule minLength(size_t n) {
    return {"至少 " + std::to_string(n) + " 个字符", [n](const std::string& s) { return s.size() >= n; }};
}
Rule hasDigit() {
    return {"至少包含一个数字", [](const std::string& s) {
                return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
            }};
}
Rule hasUpper() {
    return {"至少包含一个大写字母", [](const std::string& s) {
                return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isupper(c); });
            }};
}
Rule notCommon() {
    return {"不能使用常见密码", [](const std::string& s) {
                static const std::vector<std::string> common = {"password", "Passw0rd", "12345678", "qwerty123"};
                return std::find(common.begin(), common.end(), s) == common.end();
            }};
}

int main() {
    const std::vector<Rule> website = {minLength(8), hasDigit(), hasUpper(), notCommon()};
    const std::vector<Rule> admin = {minLength(12), hasDigit(), hasUpper(), notCommon()};
    const std::vector<Rule> tool = {minLength(6), hasDigit()};

    for (const auto& e : validate("Passw0rd", website)) std::cout << "官网：" << e << '\n';
    for (const auto& e : validate("Passw0rd", admin)) std::cout << "后台：" << e << '\n';
    for (const auto& e : validate("abc", tool)) std::cout << "内部工具：" << e << '\n';
}
```

输出和上一个版本完全相同。下面的演示里，密码会依次经过列表里的每条规则。你可以输入别的密码试试，再点「加一条规则」或「换成后台的规则」，注意右边的 `validate` 代码从头到尾没有变过：

<div class="dsviz" data-viz="dp-ocp"></div>

现在的结构是：

- **对修改关闭**：`validate` 只有 5 行，以后不管加多少规则、有多少条产品线，它都不用再改。
- **对扩展开放**：新规则就是一个新函数（`notCommon()`），新产品线就是一个新列表；调用处 `{minLength(12), hasDigit(), ...}` 读起来就是规则本身。

这里用的是 `std::function` 加 lambda：对于「只有一个函数」的扩展点，它比定义一个抽象类、再为每条规则写一个派生类要简洁得多。如果规则需要多个函数（比如还要一个 `name()` 用于日志），或者想让编译器检查每条规则都实现了哪些函数，就换成抽象类。本质上两者都是**策略模式**，只是 C++ 里有两种写法。

### 3.4 做到极致：插件

上面的例子里，加一条规则虽然不用改 `validate`，但还是要改 `main`（往列表里加一项）、重新编译整个程序。开闭原则能做到更彻底吗？可以：**插件**。主程序在运行时加载一个目录下的所有动态库，每个动态库提供一条规则。加规则时，只编译一个新的动态库扔进目录，主程序完全不用重新编译。

```uml
label: 插件结构：主程序 validator 只依赖 Rule 接口；MinLength、HasDigit、NoSpace 分别编译成独立的动态库，实现 Rule 接口，运行时被加载
classes:
  - { id: Host, name: validator（主程序）, x: 20, y: 31.5, w: 170, ops: ['+ main()'] }
  - { id: Rule, stereo: interface, x: 350, y: 16, w: 230, ops: ['*+ message() : string', '*+ ok(pwd) : bool'] }
  - { id: Min, name: MinLength, x: 260, y: 200, w: 120 }
  - { id: Digit, name: HasDigit, x: 405, y: 200, w: 120 }
  - { id: Space, name: NoSpace, x: 550, y: 200, w: 120, hl: new }
rels:
  - { from: Host, to: Rule, type: depend, label: 'dlopen + create_rule()' }
  - { from: Min, to: Rule, type: realize, mid: 160 }
  - { from: Digit, to: Rule, type: realize, mid: 160 }
  - { from: Space, to: Rule, type: realize, mid: 160 }
texts:
  - { x: 465, y: 258, text: 'plugins/*.so：每个文件单独编译', anchor: middle, muted: true }
```

主程序和插件之间唯一的约定是一个头文件（`plugin/rule.h`）：

<!-- run: off -->
```cpp
#pragma once
#include <string>

class Rule {
public:
    virtual ~Rule() = default;
    virtual std::string message() const = 0;                 // 不通过时的提示
    virtual bool ok(const std::string& pwd) const = 0;       // 是否通过
};

// 每个插件导出一个名为 create_rule 的函数。extern "C" 让函数名不经过 C++ 的名字修饰，主程序才能按名字找到它
extern "C" Rule* create_rule();
using CreateRule = Rule* (*)();
```

主程序遍历 `plugins/` 目录，用 `dlopen` 加载每个 `.so`，用 `dlsym` 找到 `create_rule` 并调用它，得到一个 `Rule` 对象：

<!-- run: off -->
```cpp
// 编译：clang++ -std=c++17 -o validator validator.cpp（Linux 上较老的 glibc 还要加 -ldl）
#include <dlfcn.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
#include "rule.h"

int main(int argc, char** argv) {
    std::string pwd = argc > 1 ? argv[1] : "Passw0rd";
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator("plugins"))
        if (entry.path().extension() == ".so") files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    std::vector<std::unique_ptr<Rule>> rules;
    for (const auto& f : files) {
        void* lib = dlopen(f.c_str(), RTLD_NOW);                       // 程序退出前不 dlclose：规则对象的代码在库里
        if (!lib) { std::cerr << dlerror() << '\n'; continue; }
        auto create = reinterpret_cast<CreateRule>(dlsym(lib, "create_rule"));
        if (!create) { std::cerr << f << " 没有 create_rule\n"; continue; }
        rules.emplace_back(create());
        std::cout << "加载 " << f.filename().string() << '\n';
    }

    int failed = 0;
    for (const auto& r : rules)
        if (!r->ok(pwd)) { std::cout << "  ✗ " << r->message() << '\n'; ++failed; }
    std::cout << (failed ? "不通过" : "通过") << '\n';
}
```

一个插件就是一个很小的源文件：

<!-- run: off -->
```cpp
#include "../rule.h"

class NoSpace : public Rule {
public:
    std::string message() const override { return "不能包含空格"; }
    bool ok(const std::string& pwd) const override { return pwd.find(' ') == std::string::npos; }
};

extern "C" Rule* create_rule() { return new NoSpace; }
```

`plugin/run.sh` 先编译主程序和前两个插件并运行，再**只编译**第三个插件放进目录，再运行一次，同时打印主程序的 md5：

```text
== 两个插件
2524e9f3c2111a92d7c9fa77edb44306  validator
加载 10_min_length.so
加载 20_has_digit.so
  ✗ 至少 8 个字符
  ✗ 至少包含一个数字
不通过
== 新增插件 30_no_space.so，主程序不重新编译
2524e9f3c2111a92d7c9fa77edb44306  validator
加载 10_min_length.so
加载 20_has_digit.so
加载 30_no_space.so
  ✗ 至少 8 个字符
  ✗ 至少包含一个数字
  ✗ 不能包含空格
不通过
```

主程序的 md5 前后完全相同，一个字节都没变，却多了一条规则。在 Ubuntu 24.04 上用 GCC 13 运行，结果一样。浏览器的插件、编辑器的扩展、Nginx 的模块，都是这个思路。

插件的代价也很明显：

- **接口一旦发布就很难改**。`Rule` 是主程序和所有插件之间的约定，给它加一个纯虚函数，所有已经编译好的插件都会失效，甚至崩溃。
- **二进制兼容**。插件和主程序最好用同一个编译器、同一个标准库编译：`std::string` 的内存布局、`new` / `delete` 用的分配器，在不同的标准库之间并不相同。所以很多插件系统的接口只用 C 风格的类型。
- **出错更隐蔽**。插件的问题要到运行时才暴露（练习 4 有一个例子）。

所以，插件只在「扩展的人不是你」时才值得：第三方开发者、其他团队、用户自己写的扩展。

### 3.5 「关闭」只针对你预料到的变化

上一篇的实测已经说明：没有对所有变化都关闭的代码。`validate` 对「加规则」是关闭的，但如果需求变成「规则要按优先级排序，前一条不通过就不再检查后面的」，它还是得改。如果新规则需要的不只是密码，比如「密码不能包含用户名」，那 `Rule` 的检查函数签名本身就不够用了（练习 3）。

开闭原则不是要求你预测所有变化，而是：

1. 先写最简单的代码；
2. 某个方向的变化真的出现了（比如第三次往 `validate` 里加参数），就在那个方向上留出扩展点；
3. 以后同一方向的变化，就只需要新增代码。

为想象中的变化提前留扩展点，和上一篇讲的过度设计是一回事。

## 4. 两条原则的关系

- **单一职责**：按「谁会要求修改」切分代码，让每一类变化只落在一个地方。
- **开闭**：在切开的地方留出扩展点，让这一类变化只需要新增代码。

先有合理的切分，才知道扩展点该留在哪里。2.2 节的 `EmployeeService` 如果不先按角色拆开，就谈不上「对修改关闭」：任何部门的需求都要改这同一个类。

## 5. 小结

- SOLID 是五条面向对象设计原则：单一职责、开闭、里氏替换、接口隔离、依赖倒置
- **单一职责**：一个模块只对一类角色负责；判断方法是问「哪些人会要求修改它」，而不是数它有几个函数
- 长得一样但代表不同业务规则的代码，不要为了「不重复」而合并。实测：财务改了共用的工时函数，人事报表的加班从 6 小时变成 3 小时
- 在 C++ 里，职责混在一个头文件中还会带来编译依赖。实测：只用到 `average()` 的源文件，包含大而全的头文件时，GCC 编译时间是只包含 `stats.h` 时的 6 倍
- **开闭**：新功能通过新增代码实现，而不是修改已有代码。C++ 里常用的扩展点有抽象类、`std::function`、插件（动态库）
- 插件能做到主程序一个字节都不变就加上新功能，代价是接口难改、要求二进制兼容、出错更隐蔽
- 「关闭」只针对预料到的变化：变化真的出现时再留扩展点，不要提前猜

## 6. 练习

1. 下面这个类承担了几个职责？分别会被谁要求修改？你会怎么拆？

   <!-- run: off -->
   ```cpp
   class Invoice {
   public:
       double total() const;              // 按税率和折扣计算总额
       void printPdf() const;             // 生成 PDF，带公司 logo 和固定版式
       void saveToDb() const;             // 写入 invoices 表
       void sendEmail(const std::string& to) const;   // 用公司的邮件服务发给客户
   };
   ```

2. 运行 2.2 节的程序，把 `limit` 改回 8，确认输出和「改动前」一致。然后回答：在 2.3 节拆分后的版本里，如果人事部要求「加班从每天第 7.5 小时开始算」，需要改哪里？财务部的工资会不会受影响？

3. 3.3 节的规则只能看到密码本身。现在要加一条规则：密码不能包含用户名。用现在的 `Rule` 能实现吗？如果不能，你会怎么改？这次改动符合开闭原则吗？

4. 插件实验里，`create_rule` 声明成了 `extern "C"`。如果把 `rule.h` 和插件源文件里的 `extern "C"` 都去掉，重新编译主程序和一个插件，运行 `./validator abc` 会发生什么？为什么这个结果比直接崩溃更危险？

<details>
<summary>点击查看答案</summary>

1. 四个职责，对应四类角色：`total()` 的计算规则由**财务**决定（税率、折扣）；`printPdf()` 的版式由**设计 / 市场**决定；`saveToDb()` 的表结构由**DBA / 运维**决定；`sendEmail()` 用哪个邮件服务、邮件模板怎么写由**运营**决定。可以拆成 `InvoiceCalculator`、`InvoicePdfRenderer`、`InvoiceRepository`、`InvoiceMailer`，`Invoice` 本身只保留发票的数据。如果项目很小、这四件事实际上都是你一个人在改，也可以先不拆，等到真的出现「改了版式，结果总额算错了」这类问题时再拆。

2. 改回 8 以后输出「工资：4900」和「Alice 正常工时 40 小时，加班 6 小时」，和改动前一致。在拆分版里，只要把 `HourReporter::kLegalRegularLimit` 改成 7.5，人事报表变成「正常工时 37.5 小时，加班 8.5 小时」；工资由 `PayCalculator` 计算，用的是它自己的 `kPaidRegularLimit`，不受影响，仍然是 4750。

3. 不能直接实现：`ok` 只接收密码，看不到用户名。有两种改法：
   - **用 lambda 捕获**：写一个 `notContain(const std::string& username)`，返回的 `Rule` 在 lambda 里捕获用户名。`Rule` 和 `validate` 都不用改，但规则列表就不能再是固定的常量，每次校验都要根据当前用户重新组装。
   - **扩大输入**：把检查函数改成接收一个结构体 `struct Input { std::string pwd, username; };`，以后再需要邮箱、生日之类的信息，往结构体里加字段就行。
   第二种改法修改了 `Rule` 和 `validate`，也就是「关闭」的那部分。这说明当初的扩展点只预料到了「规则只看密码」这一种变化，新需求超出了这个范围。这很正常：现在改一次，把扩展点改成更通用的形状，以后同类的需求就又只需要新增代码了。

4. 输出：

   ```text
   "plugins/20_has_digit.so" 没有 create_rule
   通过
   ```

   没有 `extern "C"` 时，C++ 编译器会把函数名修饰成带参数类型信息的符号，比如 GCC / Clang 上是 `_Z11create_rulev`（用 `nm` 可以看到），`dlsym(lib, "create_rule")` 按原名就找不到了。于是这个插件被跳过，规则列表是空的，**任何密码都显示「通过」**。程序不崩溃、只在错误输出里打一行提示，很容易被忽略，而校验功能其实已经完全失效了。这就是 3.4 节说的「出错更隐蔽」。对安全相关的检查，加载失败时应该直接拒绝运行，而不是带着空的规则列表继续。注意如果只去掉一处 `extern "C"`，编译器会报 `different language linkage` 错误，反而能及时发现问题。

</details>
