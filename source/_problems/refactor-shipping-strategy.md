---
id: 102
title: 重构挑战：运费计算
difficulty: medium
tags: [重构挑战, 设计模式, 策略模式]
related: [dp-uml-class-diagram]
langs: [cpp]
function: shippingCost
params: { method: string, weight: double, distance: int }
returns: double
compare: float
starter: |
  #include <string>
  using namespace std;

  // 每加一种快递方式，就要回到这个函数里再加一个分支
  class Solution {
  public:
      double shippingCost(string method, double weight, int distance) {
          if (method == "standard") {
              double cost = 5 + weight * 2;
              if (distance > 500) cost += 10;
              return cost;
          } else if (method == "express") {
              double cost = 12 + weight * 3.5;
              if (distance > 500) cost *= 1.5;
              return cost;
          } else if (method == "economy") {
              return weight <= 1 ? 3 : 3 + (weight - 1) * 1.2;
          }
          return -1;
      }
  };
checks:
  - forbid: 'method\s*(==|!=)'
    msg: 不能再用 method == "..." 一个个判断快递方式，改成按名字查表找到对应的策略对象
  - forbid: '\bswitch\b'
    msg: 不能用 switch 代替 if-else，它们是同一种写法
  - require: 'virtual[^;{]*\bcost\s*\([^)]*\)[^;{]*=\s*0\s*;'
    msg: 需要一个策略接口：带纯虚函数 cost(...) 的抽象类
  - require: '\bclass\s+\w+\s*(final\s*)?:\s*public\s+\w+'
    min: 4
    msg: 每种快递方式写成一个类，以 public 方式继承策略接口（standard、express、economy、drone 共 4 个）
examples:
  - in: ["standard", 2.0, 100]
    out: 9.0
    explain: 普通快递：5 + 2 × 2 = 9，距离没有超过 500 公里。
  - in: ["express", 2.0, 800]
    out: 28.5
    explain: 特快：(12 + 3.5 × 2) × 1.5 = 28.5，超过 500 公里要乘 1.5。
  - in: ["drone", 1.5, 10]
    out: 35.0
    explain: 新增的无人机配送：20 + 1.5 × 10 = 35。
tests:
  - { in: ["standard", 3.5, 600], out: 22.0 }
  - { in: ["standard", 0.0, 500], out: 5.0 }
  - { in: ["express", 1.0, 100], out: 15.5 }
  - { in: ["economy", 0.5, 1000], out: 3.0 }
  - { in: ["economy", 3.0, 50], out: 5.4 }
  - { in: ["drone", 6.0, 10], out: -1.0 }
  - { in: ["drone", 2.0, 25], out: -1.0 }
  - { in: ["drone", 5.0, 20], out: 50.0 }
  - { in: ["truck", 1.0, 1], out: -1.0 }
---

这是一道**重构挑战**：编辑器里已经有一份能用的代码，但它的结构经不起变化。你要做两件事：

**1. 加一个新需求：无人机配送 `"drone"`**

- 只能送重量不超过 5 公斤、距离不超过 20 公里的包裹，超出范围返回 `-1`
- 运费 = 20 + 1.5 × 距离

**2. 把代码重构成策略模式**

现在的 `shippingCost` 用一串 `if-else` 判断快递方式，每加一种方式都要改这个函数，改的时候还可能碰坏已有的分支。重构成下面的结构：

```uml
label: 重构后的结构：Solution 按名字查表找到一个 ShippingStrategy，四种快递方式各自实现 ShippingStrategy
classes:
  - { id: Solution, x: 20, y: 20, w: 290, attrs: ['- strategies_ : map<string, unique_ptr<ShippingStrategy>>'], ops: ['+ shippingCost(method, weight, distance) : double'] }
  - { id: ShippingStrategy, stereo: interface, x: 480, y: 20, w: 250, ops: ['*+ cost(weight: double, distance: int) : double'] }
  - { id: Standard, x: 370, y: 180, w: 90 }
  - { id: Express, x: 475, y: 180, w: 90 }
  - { id: Economy, x: 580, y: 180, w: 90 }
  - { id: Drone, x: 685, y: 180, w: 90 }
rels:
  - { from: Solution, to: ShippingStrategy, type: compose, tm: '*' }
  - { from: Standard, to: ShippingStrategy, type: realize, mid: 130 }
  - { from: Express, to: ShippingStrategy, type: realize, mid: 130 }
  - { from: Economy, to: ShippingStrategy, type: realize, mid: 130 }
  - { from: Drone, to: ShippingStrategy, type: realize, mid: 130 }
```

- 一个策略接口：带纯虚函数 `cost(weight, distance)` 的抽象类
- 每种快递方式一个类，实现这个接口
- `Solution` 里用一张表（比如 `map<string, unique_ptr<...>>`）按名字找到策略对象，找不到返回 `-1`

重构完成后，再加第五种快递方式只需要**新写一个类、在表里加一行**，不用碰任何已有的计算代码。

提交时除了跑用例，还会**检查代码结构**：不能再出现 `method == "..."` 这样的判断，必须有策略接口，并且至少有 4 个实现类。点「运行」时也会提示还有哪些检查没通过。

类图的画法见《UML 类图》；结果比较时允许 `1e-6` 以内的误差。
