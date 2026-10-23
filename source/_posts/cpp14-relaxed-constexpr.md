---
title: 放宽的 constexpr、变量模板与新字面量
date: 2026-10-22 14:40:00
updated: 2026-10-22 14:40:00
categories:
  - C/C++
  - C++14
tags:
  - C++14
  - constexpr
  - 模板
order: 3
description: C++14 的 constexpr 函数终于可以使用局部变量、if 和循环，编译期计算变得和写普通函数一样简单。本文还介绍变量模板、二进制字面量、数字分隔符和 [[deprecated]] 属性。
disableNunjucks: true
---

> **本文目标**：会写带循环和局部变量的 `constexpr` 函数，能在编译期生成查找表；知道编译期计算的步数限制；会用变量模板定义「同一个常量的不同类型版本」；会用二进制字面量、数字分隔符和 `[[deprecated]]`。
>
> **前置知识**：C++11 章节的《nullptr、using 别名与 constexpr 入门》。
>
> **编译环境**：`g++ -std=c++14` / `clang++ -std=c++14`，GCC 5+ / Clang 3.4+。

## 1. 放宽的 constexpr 函数

C++11 的 `constexpr` 函数只能有一条 `return` 语句，复杂一点的逻辑只能靠三目运算符和递归硬凑。判断质数这样一个简单的函数，用普通的循环写法，C++11 直接报错：

```cpp
constexpr bool isPrime(int n)
{
    if (n < 2) return false;
    for (int i = 2; i * i <= n; ++i)
        if (n % i == 0) return false;
    return true;
}
```

`-std=c++11 -pedantic-errors` 编译：

```
error: statement not allowed in constexpr function
```

C++14 取消了这些限制。`constexpr` 函数中可以使用：

- 局部变量（必须初始化，不能是 `static` 或 `thread_local`）
- `if`、`switch`
- `for`、`while`、`do-while` 循环
- 修改局部变量和参数
- 多条 `return` 语句

仍然**不能**使用的有：`goto`、`try` 块（C++20 起放宽）、`static` 局部变量、调用非 `constexpr` 函数、`new`/`delete`（C++20 起有限支持）。

### 1.1 在编译期生成查找表

```cpp
#include <iostream>

// C++14：constexpr 函数可以有局部变量、if、循环
constexpr bool isPrime(int n)
{
    if (n < 2) return false;
    for (int i = 2; i * i <= n; ++i)
        if (n % i == 0) return false;
    return true;
}

constexpr int countPrimes(int limit)
{
    int count = 0;
    for (int i = 2; i <= limit; ++i)
        if (isPrime(i)) ++count;
    return count;
}

// 编译期生成一张查找表
struct PrimeTable {
    int values[25];
};

constexpr PrimeTable makePrimeTable()
{
    PrimeTable t{};
    int k = 0;
    for (int i = 2; k < 25; ++i)
        if (isPrime(i)) t.values[k++] = i;
    return t;
}

constexpr PrimeTable kPrimes = makePrimeTable();   // 整张表在编译期算好

static_assert(isPrime(97), "97 是质数");
static_assert(!isPrime(91), "91 = 7 * 13");
static_assert(countPrimes(100) == 25, "100 以内有 25 个质数");
static_assert(kPrimes.values[24] == 97, "第 25 个质数是 97");

int main()
{
    std::cout << "100 以内的质数：";
    for (int p : kPrimes.values) std::cout << p << ' ';
    std::cout << '\n';
    int n = 0;
    std::cin >> n;
    std::cout << n << (isPrime(n) ? " 是" : " 不是") << "质数（运行期调用）\n";
    return 0;
}
```

输入 `91`，运行结果：

```
100 以内的质数：2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97 
91 不是质数（运行期调用）
```

- `isPrime` 写起来就是一个普通函数，同时能在编译期（`static_assert`）和运行期（`isPrime(n)`）使用。
- **`kPrimes` 整张表都是在编译时算出来的**，程序运行时它就是一段现成的只读数据，没有任何计算开销。这种技巧常用于 CRC 校验表、三角函数表、字符分类表等。
- 四个 `static_assert` 相当于**在编译期运行的单元测试**：函数写错了，程序根本编译不过。

> 为什么用自定义的 `PrimeTable` 结构体，而不用 `std::array`？因为 C++14 中 `std::array` 的非 const `operator[]` 还不是 `constexpr`，不能在 `constexpr` 函数里给它的元素赋值。这个问题在 C++17 中被修复。

### 1.2 constexpr 成员函数不再隐式 const

```cpp
#include <iostream>

class Point {
public:
    constexpr Point(int x, int y) : x_(x), y_(y) {}
    constexpr int x() const { return x_; }
    constexpr int y() const { return y_; }
    constexpr void move(int dx, int dy) { x_ += dx; y_ += dy; }   // C++14：可以修改成员
private:
    int x_, y_;
};

constexpr Point makeMoved()
{
    Point p(1, 2);
    p.move(10, 20);              // 在编译期修改对象
    return p;
}

int main()
{
    constexpr Point p = makeMoved();
    static_assert(p.x() == 11 && p.y() == 22, "");
    std::cout << "p = (" << p.x() << ", " << p.y() << ")\n";
    return 0;
}
```

运行结果：

```
p = (11, 22)
```

C++11 中，`constexpr` 成员函数**自动带有 `const`**，所以不可能写出修改成员的 `constexpr` 函数。C++14 去掉了这个隐式的 `const`，`move` 这样的函数也能是 `constexpr` 的，于是可以在编译期创建对象后再修改它。

> 这个改动有一个副作用：C++11 中写成 `constexpr int x() { ... }`（没写 `const`）的代码，在 C++14 中就不再是 const 成员函数，用 const 对象调用会编译失败。所以 `constexpr` 的只读成员函数要明确写上 `const`。

## 2. 编译期计算的限制

编译期计算再方便，也是在**编译器里**运行的。为了防止死循环让编译永远不结束，编译器限制了编译期计算的步数：

```cpp
constexpr long long slowSum(long long n)
{
    long long s = 0;
    for (long long i = 0; i < n; ++i) s += i;
    return s;
}
constexpr long long r = slowSum(100000000);   // 一亿次循环
```

Clang 的报错：

```
error: constexpr variable 'r' must be initialized by a constant expression
note: constexpr evaluation hit maximum step limit; possible infinite loop?
note: in call to 'slowSum(100000000)'
```

Clang 默认的上限是 1048576 步（可以用 `-fconstexpr-steps=N` 调整），GCC 用 `-fconstexpr-loop-limit` 和 `-fconstexpr-ops-limit` 控制。编译期计算比运行期慢得多，**适合做规模不大、但结果需要在编译期确定的计算**，大规模计算应该放到运行期，或者用脚本预先生成数据文件。

## 3. 变量模板

C++11 有函数模板、类模板，还有别名模板。C++14 新增了**变量模板**：一个变量可以有模板参数，每个模板参数对应一个不同的变量。

```cpp
#include <iomanip>
#include <iostream>
#include <type_traits>

// 变量模板：同一个常量，不同的类型
template <typename T>
constexpr T pi = T(3.1415926535897932385L);

// 用变量模板简化类型特征
template <typename T>
constexpr bool isNumber = std::is_arithmetic<T>::value;

template <typename T>
T circleArea(T r) { return pi<T> * r * r; }

int main()
{
    std::cout << std::setprecision(20)
              << "pi<float>  = " << pi<float> << '\n'
              << "pi<double> = " << pi<double> << '\n'
              << "pi<int>    = " << pi<int> << '\n';
    std::cout << std::setprecision(6) << "circleArea(2.0f) = " << circleArea(2.0f) << '\n';
    static_assert(isNumber<int> && isNumber<double> && !isNumber<const char*>, "");
    return 0;
}
```

运行结果：

```
pi<float>  = 3.1415927410125732422
pi<double> = 3.141592653589793116
pi<int>    = 3
circleArea(2.0f) = 12.5664
```

两个典型用途：

1. **数学常量**：`pi<float>` 和 `pi<double>` 是两个不同的变量，各自具有对应类型的精度。在 `circleArea<T>` 这样的模板中，`pi<T>` 自动匹配参数类型，不会出现 `float` 和 `double` 混合运算。C++20 的 `<numbers>` 头文件提供了标准的 `std::numbers::pi_v<T>`，用的就是变量模板。
2. **简化类型特征**：C++11 中查询类型特征要写 `std::is_arithmetic<T>::value`，变量模板可以把它包装成 `isNumber<T>`。C++17 的标准库为所有类型特征都提供了这样的简写，比如 `std::is_integral_v<T>`。

## 4. 二进制字面量与数字分隔符

```cpp
#include <bitset>
#include <cstdint>
#include <iostream>

int main()
{
    int flags = 0b1010'0110;                 // 二进制字面量 + 数字分隔符
    long long population = 1'412'000'000;     // 十四亿一千二百万，一眼看清位数
    std::uint32_t color = 0xFF'88'00'FF;      // 十六进制也可以分组：RR GG BB AA
    double avogadro = 6.022'140'76e23;

    std::cout << "flags = " << flags << " = 0b" << std::bitset<8>(flags) << '\n';
    std::cout << "population = " << population << '\n';
    std::cout << std::hex << "color = 0x" << color << std::dec << '\n';
    std::cout << "avogadro = " << avogadro << '\n';
    return 0;
}
```

运行结果：

```
flags = 166 = 0b10100110
population = 1412000000
color = 0xff8800ff
avogadro = 6.02214e+23
```

- **二进制字面量** `0b` 或 `0B` 开头。以前只能写十进制、八进制（`0` 开头）和十六进制（`0x` 开头），处理位标志、硬件寄存器时，二进制写法一目了然。
- **数字分隔符**是单引号 `'`，可以放在数字中任意两个数字之间，编译器会忽略它，只是为了方便阅读。为什么不用逗号或下划线？逗号已经是运算符，下划线会和用户自定义字面量的后缀冲突。
- 输出时不会带分隔符，`std::cout` 不认识它，它只存在于源代码中。

## 5. [[deprecated]] 属性

C++11 引入了**属性**（attribute）语法 `[[...]]`，用来给编译器提供额外的信息。C++14 新增了 `[[deprecated]]`，标记一个函数、类、变量等**已弃用**：

```cpp
[[deprecated("请改用 newApi()，oldApi 将在 2.0 版本删除")]]
int oldApi() { return 1; }
int newApi() { return 2; }
int main() { return oldApi() + newApi(); }
```

Clang 的编译输出：

```
warning: 'oldApi' is deprecated: 请改用 newApi()，oldApi 将在 2.0 版本删除 [-Wdeprecated-declarations]
```

代码仍然能编译，只是在每个使用它的地方给出警告，提醒调用者迁移到新接口。编写库的时候，删除一个接口之前先标记为 `[[deprecated]]`，给使用者留出修改代码的时间，这是很好的做法。

C++17 还会加入 `[[nodiscard]]`、`[[maybe_unused]]`、`[[fallthrough]]` 等常用属性，在 C++17 章节介绍。

## 6. 常见错误

1. **以为 `constexpr` 函数一定在编译期执行**：和 C++11 一样，只有在必须得到常量的地方才保证编译期计算。
2. **`constexpr` 函数中调用了普通函数**：比如在 `constexpr` 函数中调用 `std::sqrt`（C++26 之前不是 `constexpr`），在编译期调用时会报错。在运行期调用则没有问题，所以这类错误可能要到有人在编译期使用它时才暴露出来，最好用 `static_assert` 测一下。
3. **数字分隔符放错位置**：不能放在开头或结尾（`'1000`、`1000'`），不能连续（`1''000`），也不能紧挨着前缀或小数点（`0x'FF`、`1'.5`）。

## 7. 进阶：编译期计算与运行期计算的选择

`constexpr` 函数的一大优势是**同一份代码可以在两种场合使用**：

```cpp
constexpr auto table = makePrimeTable();   // 编译期：结果作为常量数据嵌入程序
auto runtimeTable = makePrimeTable();      // 运行期：程序启动后计算
```

什么时候该用编译期计算？

- **输入在编译期就确定**（常量、配置、固定的表格）；
- **计算量适中**（受步数限制，也会拖慢编译）；
- **需要把结果用在常量表达式中**（数组长度、模板参数、`switch` 的 `case`、`static_assert`）。

编译期计算还有一个隐藏的好处：**编译期计算中不允许出现未定义行为**。整数溢出、数组越界、除以零在运行期是未定义行为，可能悄悄地产生错误结果；而在编译期计算中会直接导致编译失败：

```cpp
#include <climits>
constexpr int add(int a, int b) { return a + b; }
constexpr int r = add(INT_MAX, 1);
```

```
error: constexpr variable 'r' must be initialized by a constant expression
note: value 2147483648 is outside the range of representable values of type 'int'
```

所以用 `static_assert` 测试 `constexpr` 函数，还能顺便检查出这类 bug。

## 8. 小结

- C++14 的 `constexpr` 函数可以有局部变量、`if`、循环、多条 `return`，写起来和普通函数一样
- 可以在编译期生成查找表，用 `static_assert` 做编译期单元测试
- `constexpr` 成员函数不再隐式 `const`，可以在编译期修改对象；只读的成员函数要明确写 `const`
- 编译期计算有步数限制，适合规模适中的计算
- **变量模板**：`template <typename T> constexpr T pi = ...;`，用于类型相关的常量和类型特征简写
- **二进制字面量** `0b1010` 和**数字分隔符** `1'000'000` 让数字更易读
- **`[[deprecated("原因")]]`** 标记已弃用的接口

## 9. 练习

1. 用 C++14 的 `constexpr` 写一个函数 `gcd(a, b)`（最大公约数），用 `static_assert` 验证 `gcd(48, 18) == 6`。
2. 写一个 `constexpr` 函数，在编译期计算 `0` 到 `255` 每个数的二进制表示中 `1` 的个数，生成一张查找表 `popcountTable`，并用它实现运行期的 `popcount(std::uint32_t x)`（每次查 4 个字节）。
3. 定义一个变量模板 `maxValue<T>`，等于类型 `T` 能表示的最大值（提示：`std::numeric_limits<T>::max()` 是 `constexpr` 的）。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   constexpr int gcd(int a, int b)
   {
       while (b != 0) {
           int t = a % b;
           a = b;
           b = t;
       }
       return a;
   }
   static_assert(gcd(48, 18) == 6, "");
   ```

2. 参考代码：

   ```cpp
   struct PopTable { unsigned char v[256]; };

   constexpr PopTable makePopTable()
   {
       PopTable t{};
       for (int i = 0; i < 256; ++i) {
           int n = i, c = 0;
           while (n) { c += n & 1; n >>= 1; }
           t.v[i] = static_cast<unsigned char>(c);
       }
       return t;
   }
   constexpr PopTable popcountTable = makePopTable();
   static_assert(popcountTable.v[0xFF] == 8, "");

   int popcount(std::uint32_t x)
   {
       return popcountTable.v[x & 0xFF] + popcountTable.v[(x >> 8) & 0xFF]
            + popcountTable.v[(x >> 16) & 0xFF] + popcountTable.v[x >> 24];
   }
   ```

3. `template <typename T> constexpr T maxValue = std::numeric_limits<T>::max();`，需要包含 `<limits>`。

</details>
