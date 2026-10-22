---
title: 万能引用与完美转发
date: 2026-10-23 09:00:00
updated: 2026-10-23 09:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 完美转发
  - 模板
order: 6
description: 模板中的 T&& 既能绑定左值又能绑定右值，配合 std::forward 可以把参数原封不动地传给下一个函数。本文讲清楚万能引用、引用折叠、std::forward 的原理，以及 emplace_back 背后的机制。
disableNunjucks: true
---

> **本文目标**：分清万能引用和右值引用；理解引用折叠规则；会用 `std::forward` 写转发函数；明白 `emplace_back` 为什么比 `push_back` 少一次移动；知道完美转发的几个失败场景。
>
> **前置知识**：上一篇《右值引用与移动语义》，函数模板的基本写法。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. 问题：包装函数会「丢掉」右值属性

上一篇讲过，同一个函数可以按左值、右值重载：

```cpp
void process(int& x);    // 处理左值
void process(int&& x);   // 处理右值，可以“偷”资源
```

现在要写一个包装函数 `wrapper`，在调用 `process` 之前做点别的事，比如打日志、计时。我们希望：**传给 `wrapper` 的是左值，就调用左值版本；传的是右值，就调用右值版本。**

```cpp
#include <iostream>
#include <utility>

void process(int& x)  { std::cout << "  process(int&)  左值版本, x = " << x << '\n'; }
void process(int&& x) { std::cout << "  process(int&&) 右值版本, x = " << x << '\n'; }

// 尝试 1：普通的 T&& 参数，直接传下去
template <typename T>
void wrapperBad(T&& arg)
{
    process(arg);                    // arg 有名字，永远是左值
}

// 尝试 2：用 std::forward 完美转发
template <typename T>
void wrapperGood(T&& arg)
{
    process(std::forward<T>(arg));   // 左值进来就转发成左值，右值进来就转发成右值
}

int main()
{
    int n = 1;
    std::cout << "wrapperBad(n):\n";   wrapperBad(n);
    std::cout << "wrapperBad(2):\n";   wrapperBad(2);
    std::cout << "wrapperGood(n):\n";  wrapperGood(n);
    std::cout << "wrapperGood(2):\n";  wrapperGood(2);
    return 0;
}
```

运行结果：

```
wrapperBad(n):
  process(int&)  左值版本, x = 1
wrapperBad(2):
  process(int&)  左值版本, x = 2
wrapperGood(n):
  process(int&)  左值版本, x = 1
wrapperGood(2):
  process(int&&) 右值版本, x = 2
```

`wrapperBad(2)` 传进去的是右值 `2`，调用的却是左值版本。原因上一篇讲过：**参数 `arg` 有名字，所以表达式 `arg` 是左值**，不管它的类型是什么。`wrapperGood` 用 `std::forward<T>(arg)` 恢复了参数原来的「左值 / 右值」属性，这就叫**完美转发**（perfect forwarding）。

要理解它为什么能工作，需要先弄清两件事：`T&&` 在模板里的特殊含义，以及引用折叠。

## 2. 万能引用

上一篇说过，`int&&` 只能绑定右值。可是上面的 `wrapperGood(n)` 传入的是左值 `n`，为什么能编译通过？

因为**在函数模板中，形如 `T&&` 且 `T` 需要推导的参数，并不是右值引用**，而是一种特殊的引用，它**既能绑定左值，也能绑定右值**。Scott Meyers 把它叫做**万能引用**（universal reference），C++ 标准里的正式名称是**转发引用**（forwarding reference）。

### 2.1 怎么判断是不是万能引用

必须**同时满足**两个条件：

1. 形式**恰好**是 `T&&`（或 `auto&&`）；
2. `T` 需要**类型推导**。

```cpp
#include <vector>
template <typename T> void f1(T&& x);                   // 万能引用
template <typename T> void f2(std::vector<T>&& v);      // 右值引用：形式不是 T&&
template <typename T> void f3(const T&& x);             // 右值引用：多了 const
template <typename T> struct Box {
    void put(T&& x);                                    // 右值引用：T 在类实例化时已确定，没有推导
};
int main()
{
    int n = 0;
    std::vector<int> v;
    f1(n);              // OK
    f2(v);              // 错误：右值引用不能绑定左值 v
    f3(n);              // 错误
    Box<int> b;
    b.put(n);           // 错误
    auto&& r = n;       // auto&& 也是万能引用，绑定左值 OK
    (void)r;
}
```

Clang 的报错：

```
error: no matching function for call to 'f2'
error: no matching function for call to 'f3'
error: rvalue reference to type 'int' cannot bind to lvalue of type 'int'
```

只有 `f1` 和 `auto&&` 是万能引用。`Box<int>::put` 的参数在 `Box<int>` 实例化时就已经确定为 `int&&`，调用 `put` 时不需要推导，所以它是普通的右值引用。

### 2.2 T 被推导成了什么

万能引用的特殊推导规则是：

- 传入**左值**（类型为 `X`）时，`T` 被推导为 **`X&`**；
- 传入**右值**（类型为 `X`）时，`T` 被推导为 **`X`**。

```cpp
#include <iostream>
#include <string>
#include <type_traits>

template <typename T>
void show(T&& arg)
{
    std::cout << (std::is_lvalue_reference<T>::value ? "T = X&,  " : "T = X,   ")
              << (std::is_lvalue_reference<decltype(arg)>::value ? "arg 的类型是左值引用" : "arg 的类型是右值引用")
              << '\n';
}

int main()
{
    int n = 0;
    const int cn = 0;
    std::string s = "hi";

    show(n);              // 左值
    show(cn);             // const 左值
    show(10);             // 右值
    show(std::move(s));   // 右值
    show(s);              // 左值

    // 引用折叠：用 typedef 手动制造 “引用的引用”
    typedef int& LRef;
    typedef int&& RRef;
    static_assert(std::is_same<LRef&,  int&>::value,  "& &   -> &");
    static_assert(std::is_same<LRef&&, int&>::value,  "& &&  -> &");
    static_assert(std::is_same<RRef&,  int&>::value,  "&& &  -> &");
    static_assert(std::is_same<RRef&&, int&&>::value, "&& && -> &&");
    std::cout << "引用折叠规则验证通过\n";
    return 0;
}
```

运行结果：

```
T = X&,  arg 的类型是左值引用
T = X&,  arg 的类型是左值引用
T = X,   arg 的类型是右值引用
T = X,   arg 的类型是右值引用
T = X&,  arg 的类型是左值引用
引用折叠规则验证通过
```

传入左值 `n` 时，`T` 是 `int&`，参数类型 `T&&` 就变成了 `int& &&`，这是「引用的引用」。C++ 不允许直接写引用的引用，但在模板推导、`typedef` 这类场合，编译器会按**引用折叠**规则把它化简。

## 3. 引用折叠

规则只有一句话：**只要有一个是左值引用，结果就是左值引用；两个都是右值引用，结果才是右值引用。**

| 组合 | 结果 |
| --- | --- |
| `X& &` | `X&` |
| `X& &&` | `X&` |
| `X&& &` | `X&` |
| `X&& &&` | `X&&` |

上面代码中的四个 `static_assert` 就是在验证这张表。代入万能引用：

| 实参 | `T` 推导为 | 参数 `T&&` 展开 | 折叠后 |
| --- | --- | --- | --- |
| 左值 `int` | `int&` | `int& &&` | `int&` |
| 右值 `int` | `int` | `int&&` | `int&&` |

于是 `T&&` 遇到左值就变成左值引用，遇到右值就变成右值引用，这就是它「万能」的原因。更重要的是，**`T` 本身记录了实参原来是左值还是右值**：`T` 是引用类型说明传进来的是左值，不是引用类型说明传进来的是右值。`std::forward` 正是利用了这个信息。

## 4. std::forward 的原理

`std::forward` 的实现（简化版）：

```cpp
template <typename T>
T&& forward(typename std::remove_reference<T>::type& arg) noexcept
{
    return static_cast<T&&>(arg);
}
```

关键是 `static_cast<T&&>`，结合引用折叠来看：

- 传入左值时，`T = int&`，`T&&` 折叠为 `int&`，返回**左值引用**，结果是左值；
- 传入右值时，`T = int`，`T&&` 就是 `int&&`，返回**右值引用**，结果是右值。

和 `std::move` 对比：

| | `std::move(x)` | `std::forward<T>(x)` |
| --- | --- | --- |
| 作用 | **无条件**转换成右值 | **有条件**转换：原来是右值才转成右值 |
| 模板参数 | 自动推导 | **必须显式写出 `<T>`** |
| 用在哪里 | 确定不再需要某个对象时 | 万能引用参数，转发给下一个函数 |

`std::forward<T>` 的 `T` 不能省略，它正是靠这个 `T` 才知道参数原来是什么。

**规则：对万能引用参数使用 `std::forward`，对右值引用参数使用 `std::move`。**

## 5. 应用：emplace_back 与工厂函数

完美转发最常见的用途，是把参数**原样**传给构造函数，在目标位置直接构造对象。标准库容器的 `emplace_back` 就是这样实现的：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Student {
    Student(std::string name, int age) : name(std::move(name)), age(age)
    {
        std::cout << "  构造 Student(" << this->name << ", " << age << ")\n";
    }
    Student(const Student& o) : name(o.name), age(o.age) { std::cout << "  拷贝\n"; }
    Student(Student&& o) noexcept : name(std::move(o.name)), age(o.age) { std::cout << "  移动\n"; }
    std::string name;
    int age;
};

// 一个简化版的工厂函数：把参数原封不动地转发给构造函数
template <typename T, typename A1, typename A2>
std::unique_ptr<T> create(A1&& a1, A2&& a2)
{
    return std::unique_ptr<T>(new T(std::forward<A1>(a1), std::forward<A2>(a2)));
}

int main()
{
    std::vector<Student> v;
    v.reserve(4);                        // 预留空间，避免扩容干扰输出

    std::cout << "push_back(Student(\"Alice\", 20)):\n";
    v.push_back(Student("Alice", 20));   // 先构造临时对象，再移动进 vector

    std::cout << "emplace_back(\"Bob\", 21):\n";
    v.emplace_back("Bob", 21);           // 参数被完美转发，直接在 vector 内部构造

    std::cout << "create<Student>(\"Carol\", 22):\n";
    auto p = create<Student>("Carol", 22);
    std::cout << "  p->name = " << p->name << '\n';
    return 0;
}
```

运行结果：

```
push_back(Student("Alice", 20)):
  构造 Student(Alice, 20)
  移动
emplace_back("Bob", 21):
  构造 Student(Bob, 21)
create<Student>("Carol", 22):
  构造 Student(Carol, 22)
  p->name = Carol
```

- `push_back` 的参数是一个 `Student` 对象，所以必须先在外面构造一个临时对象，再把它**移动**到 vector 的内存里。
- `emplace_back` 接收的是**构造函数的参数**，它把 `"Bob"` 和 `21` 完美转发给 `Student` 的构造函数，**直接在 vector 的内存里构造**，连移动都省了。

`create` 只能接收两个参数，而 `emplace_back` 能接收任意个参数，用的是**可变参数模板**（`template <typename... Args>`），会在后面的文章中讲解。C++14 的 `std::make_unique` 也是同样的原理。

## 6. 常见错误

### 6.1 万能引用劫持拷贝构造函数

把万能引用用在构造函数上要非常小心：

```cpp
#include <iostream>
#include <string>
#include <utility>

class Person {
public:
    template <typename T>
    explicit Person(T&& name) : name_(std::forward<T>(name))
    {
        std::cout << "  模板构造函数\n";
    }
    Person(const Person& other) : name_(other.name_)
    {
        std::cout << "  拷贝构造函数\n";
    }
private:
    std::string name_;
};

int main()
{
    Person a("Alice");
    const Person b("Bob");
    std::cout << "Person c(b)   // b 是 const：\n";
    Person c(b);
    std::cout << "Person d(a)   // a 不是 const：\n";
    Person d(a);       // 想拷贝，却匹配了模板，用 Person 去初始化 std::string，编译失败
    return 0;
}
```

Clang 的报错：

```
error: no matching constructor for initialization of 'std::string' (aka 'basic_string<char>')
note: in instantiation of function template specialization 'Person::Person<Person &>' requested here
```

`Person d(a);` 中 `a` 是非 const 的左值。拷贝构造函数的参数是 `const Person&`，需要加 const；而模板把 `T` 推导为 `Person&`，参数 `Person&` 是**完全匹配**，比拷贝构造函数更好，于是编译器选择了模板，接着试图用 `Person` 初始化 `std::string`，编译失败。`const Person b` 则不会出问题，因为这时两者都是完全匹配，非模板函数优先。

**建议：不要在接收单个参数的构造函数上使用万能引用。** 对于上面这种需求，直接按值传参 `explicit Person(std::string name) : name_(std::move(name)) {}` 就足够高效了。确实需要时，要用 `std::enable_if` 排除 `Person` 类型，这属于模板元编程的内容。

### 6.2 对同一个参数 forward 多次

```cpp
template <typename T>
void twice(T&& x)
{
    use(std::forward<T>(x));
    use(std::forward<T>(x));   // 如果 x 是右值，第一次可能已经被移走了
}
```

和 `std::move` 一样，`std::forward` 之后不应该再使用这个参数。需要多次使用时，只在**最后一次**使用时 `forward`。

### 6.3 忘记写模板参数

`std::forward(x)` 无法编译，必须写 `std::forward<T>(x)`。`forward` 的参数类型是 `remove_reference<T>::type&`，这种写法故意让 `T` 无法被推导。

## 7. 进阶：完美转发也会失败

「完美」转发也有转发不了的东西。下面是几种典型情况：

### 7.1 花括号初始化列表

```cpp
#include <vector>
#include <utility>
void take(const std::vector<int>& v);
template <typename T> void fwd(T&& x) { take(std::forward<T>(x)); }
int main()
{
    take({1, 2, 3});       // OK：直接调用，花括号列表可以初始化 vector
    fwd({1, 2, 3});        // 错误：模板无法从花括号列表推导 T
}
```

Clang 的报错：

```
error: no matching function for call to 'fwd'
note: candidate template ignored: couldn't infer template argument 'T'
```

花括号列表没有类型，模板参数推导不认它。解决办法是先用 `auto` 接一下：`auto list = {1, 2, 3}; fwd(list);`，此时 `list` 是 `std::initializer_list<int>`。这也是为什么 `v.emplace_back({1, 2})` 无法编译，而 `v.push_back({1, 2})` 可以。

### 7.2 0 和 NULL 作为空指针

`fwd(0)` 会把 `T` 推导为 `int`，转发出去的是整数而不是空指针。用 `nullptr` 就没有问题，这也是《nullptr、using 别名与 constexpr 入门》里提到的原因。

### 7.3 重载函数名和函数模板名

```cpp
void callback(int);
void callback(double);
fwd(callback);           // 错误：不知道要哪个 callback
fwd(static_cast<void(*)(int)>(callback));   // OK：明确指定
```

直接调用 `take(callback)` 时，编译器可以根据 `take` 的参数类型选出合适的重载；而模板推导时还没有目标类型，无从选择。

### 7.4 位域

位域（bit-field）不能被非 const 引用绑定，因为它可能不是从一个字节边界开始的，无法取地址。所以位域不能直接传给万能引用，需要先拷贝到一个普通变量里。

## 8. 小结

- 在函数模板中，**`T&&`（且 `T` 需要推导）是万能引用**，能绑定左值和右值；`auto&&` 也是。`const T&&`、`vector<T>&&`、类模板成员函数里的 `T&&` 都不是。
- 传入左值时 `T` 推导为 `X&`，传入右值时推导为 `X`，再经过**引用折叠**得到最终类型。
- 引用折叠：有一个 `&` 就是 `&`，两个 `&&` 才是 `&&`。
- `std::forward<T>(x)` 根据 `T` 恢复参数原来的左值 / 右值属性；`std::move` 则是无条件转成右值。
- `emplace_back` 通过完美转发在容器内部直接构造对象，比 `push_back` 少一次移动。
- 不要在单参数构造函数上使用万能引用，它会劫持拷贝构造。

## 9. 练习

1. 下面哪些参数是万能引用？

   ```cpp
   template <typename T> void a(T&& x);
   template <typename T> void b(const T& x);
   template <typename T> void c(std::vector<T>&& x);
   void d(int&& x);
   template <typename T> class W { template <typename U> void e(U&& x); };
   auto&& f = g();
   ```

2. 写一个函数模板 `timed`，接收一个参数，打印「开始」，把参数完美转发给 `process`，再打印「结束」。

3. 下面代码中，`T` 分别被推导成什么？`x` 的类型是什么？

   ```cpp
   template <typename T> void h(T&& x);
   int i = 0; const int ci = 0;
   h(i); h(ci); h(42); h(std::move(i));
   ```

<details>
<summary>点击查看答案</summary>

1. `a`、`e`、`f` 是万能引用。`e` 是成员函数模板，`U` 在调用时需要推导，所以是万能引用；`b` 是 const 左值引用；`c` 形式不是 `T&&`；`d` 没有模板推导。
2. 参考代码：

   ```cpp
   template <typename T>
   void timed(T&& arg)
   {
       std::cout << "开始\n";
       process(std::forward<T>(arg));
       std::cout << "结束\n";
   }
   ```

3. `h(i)`：`T = int&`，`x` 是 `int&`；`h(ci)`：`T = const int&`，`x` 是 `const int&`；`h(42)`：`T = int`，`x` 是 `int&&`；`h(std::move(i))`：`T = int`，`x` 是 `int&&`。

</details>
