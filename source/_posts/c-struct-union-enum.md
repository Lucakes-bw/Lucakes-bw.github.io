---
title: 结构体、联合与枚举
date: 2026-10-23 17:40:00
updated: 2026-10-23 17:40:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 结构体
  - 内存对齐
order: 12
description: 结构体把不同类型的数据组合成一个整体，是 C 语言组织数据的核心工具。本文介绍结构体的定义、初始化、访问和传参，用 sizeof 和 offsetof 实测内存对齐，复现浅拷贝导致的重复释放，并介绍联合、带标记的联合、枚举，以及作为链表节点的自引用结构体。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会定义和使用结构体，掌握 `.` 和 `->` 两种访问方式；知道结构体应该传指针而不是传值；理解内存对齐，会通过调整成员顺序减小结构体的大小；认识结构体赋值的浅拷贝问题；会使用联合和枚举；会定义链表节点。
>
> **前置知识**：《指针进阶》、《动态内存》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`，64 位系统。

## 1. 结构体

一个学生有姓名、学号、成绩，它们的类型各不相同。用三个独立的数组分别存放，很容易对不上号。**结构体**（struct）可以把它们组合成一个整体：

```c
#include <stdio.h>

struct Student {
    char name[20];
    int id;
    double score;
};

typedef struct {
    double x;
    double y;
} Point;

void printStudent(const struct Student* s)      // 传指针：不复制整个结构体
{
    printf("%-6s 学号 %d，成绩 %.1f\n", s->name, s->id, s->score);
}

void addBonus(struct Student* s, double bonus)
{
    s->score += bonus;                          // 通过指针修改调用者的结构体
}

int main(void)
{
    struct Student tom = {"Tom", 1001, 85.5};
    struct Student amy = {.id = 1002, .name = "Amy", .score = 92.0};   // 指定成员初始化
    printStudent(&tom);
    printStudent(&amy);

    addBonus(&tom, 5);
    printf("加分后 tom.score = %.1f\n", tom.score);

    Point p = {3, 4};
    Point q = p;                                // 结构体可以整体赋值（复制所有成员）
    q.x = 10;
    printf("p = (%.0f, %.0f)，q = (%.0f, %.0f)\n", p.x, p.y, q.x, q.y);

    struct Student cls[3] = {
        {"Ann", 1, 78}, {"Ben", 2, 95}, {"Cat", 3, 88},
    };
    double sum = 0;
    for (int i = 0; i < 3; ++i) {
        sum += cls[i].score;
    }
    printf("平均分 %.1f，sizeof(struct Student) = %zu\n", sum / 3, sizeof(struct Student));
    return 0;
}
```

运行结果：

```
Tom    学号 1001，成绩 85.5
Amy    学号 1002，成绩 92.0
加分后 tom.score = 90.5
p = (3, 4)，q = (10, 4)
平均分 87.0，sizeof(struct Student) = 32
```

- **定义**：`struct 名字 { 成员列表 };`，注意最后的**分号**。使用时类型名是 `struct Student`。
- **`typedef`** 给类型起一个别名。`typedef struct { ... } Point;` 之后可以直接写 `Point`，不用每次都写 `struct`。
- **初始化**：按顺序 `{"Tom", 1001, 85.5}`，或者用 C99 的**指定成员初始化** `{.id = 1002, .name = "Amy"}`，可以不按顺序，没有写出的成员为 0。
- **访问成员**：结构体变量用 **`.`**（`tom.score`），结构体指针用 **`->`**（`s->score`）。`s->score` 是 `(*s).score` 的简写。
- **结构体可以整体赋值**，会复制所有成员（包括其中的数组）。这一点和数组不同。但结构体**不能用 `==` 比较**：

  ```
  error: invalid operands to binary expression ('P' and 'P')
  ```

  需要逐个成员比较。

### 1.1 传指针，而不是传值

结构体按值传递时，**整个结构体都会被复制**。一个 4 KB 的结构体，调用一百万次：

```c
typedef struct {
    double data[512];                           // 4 KB
} Big;

double byValue(Big b) { return b.data[0]; }
double byPointer(const Big* b) { return b->data[0]; }
```

```
sizeof(Big) = 4096，调用 1000000 次：按值 67.0 ms，按指针 0.9 ms
```

（`-O0` 编译。开启 `-O2` 后，这个简单的函数被内联，编译器省掉了复制，两者的差距消失了。但在实际的程序中，不能指望编译器总能优化掉。）

**规则：结构体参数传指针。只读的用 `const 类型*`，需要修改的用 `类型*`。** 这和 C++ 中「传 `const T&`」是同一个道理。

## 2. 内存对齐

`struct Student` 的成员大小是 20 + 4 + 8 = 32，`sizeof` 也是 32，正好相等。但看下面的例子：

```c
#include <stddef.h>
#include <stdio.h>

struct Bad {
    char a;
    double b;
    char c;
    int d;
};

struct Good {
    double b;
    int d;
    char a;
    char c;
};

int main(void)
{
    printf("sizeof(struct Bad)  = %zu\n", sizeof(struct Bad));
    printf("  a 在 %zu，b 在 %zu，c 在 %zu，d 在 %zu\n",
           offsetof(struct Bad, a), offsetof(struct Bad, b), offsetof(struct Bad, c), offsetof(struct Bad, d));
    printf("sizeof(struct Good) = %zu\n", sizeof(struct Good));
    printf("  b 在 %zu，d 在 %zu，a 在 %zu，c 在 %zu\n",
           offsetof(struct Good, b), offsetof(struct Good, d), offsetof(struct Good, a), offsetof(struct Good, c));
    printf("成员大小之和: %zu\n", sizeof(char) * 2 + sizeof(double) + sizeof(int));
    return 0;
}
```

运行结果：

```
sizeof(struct Bad)  = 24
  a 在 0，b 在 8，c 在 16，d 在 20
sizeof(struct Good) = 16
  b 在 0，d 在 8，a 在 12，c 在 13
成员大小之和: 14
```

（`offsetof(类型, 成员)` 来自 `<stddef.h>`，给出成员相对于结构体开头的字节偏移。）

<figure class="diagram">
<svg viewBox="0 0 680 170" role="img" aria-label="内存对齐示意图：struct Bad 中 char 后面有 7 字节填充，总共 24 字节；struct Good 按成员大小从大到小排列，只有 2 字节填充，总共 16 字节">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="22" style="fill:var(--fg)">struct Bad（24 字节）</text>
    <g transform="translate(10,32)">
      <rect x="0" y="0" width="24" height="30" style="fill:var(--accent);opacity:.75"/>
      <rect x="24" y="0" width="168" height="30" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:4 3"/>
      <rect x="192" y="0" width="192" height="30" style="fill:var(--warn);opacity:.75"/>
      <rect x="384" y="0" width="24" height="30" style="fill:var(--accent);opacity:.75"/>
      <rect x="408" y="0" width="72" height="30" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:4 3"/>
      <rect x="480" y="0" width="96" height="30" style="fill:var(--link);opacity:.75"/>
      <text x="7" y="20" style="fill:var(--bg)">a</text>
      <text x="80" y="20" style="fill:var(--muted)">填充 7</text>
      <text x="270" y="20" style="fill:var(--bg)">b</text>
      <text x="391" y="20" style="fill:var(--bg)">c</text>
      <text x="420" y="20" style="fill:var(--muted)">填充 3</text>
      <text x="522" y="20" style="fill:var(--bg)">d</text>
    </g>
    <text x="10" y="102" style="fill:var(--fg)">struct Good（16 字节）</text>
    <g transform="translate(10,112)">
      <rect x="0" y="0" width="192" height="30" style="fill:var(--warn);opacity:.75"/>
      <rect x="192" y="0" width="96" height="30" style="fill:var(--link);opacity:.75"/>
      <rect x="288" y="0" width="24" height="30" style="fill:var(--accent);opacity:.75"/>
      <rect x="312" y="0" width="24" height="30" style="fill:var(--accent);opacity:.75"/>
      <rect x="336" y="0" width="48" height="30" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:4 3"/>
      <text x="90" y="20" style="fill:var(--bg)">b</text>
      <text x="234" y="20" style="fill:var(--bg)">d</text>
      <text x="295" y="20" style="fill:var(--bg)">a</text>
      <text x="319" y="20" style="fill:var(--bg)">c</text>
      <text x="341" y="20" style="fill:var(--muted)">填 2</text>
    </g>
    <text x="600" y="52" style="fill:var(--muted)">每格 1 字节</text>
  </g>
</svg>
</figure>

两个结构体的成员完全相同，只是顺序不同，大小却分别是 24 和 16，而成员大小之和只有 14。多出来的空间是编译器插入的**填充**（padding）：

- **对齐规则**：大多数类型的变量，地址必须是它自身大小的倍数。`double` 的地址必须是 8 的倍数，`int` 必须是 4 的倍数。CPU 访问对齐的数据更快，有些 CPU 甚至根本不支持访问不对齐的数据。
- `struct Bad` 中，`a` 在偏移 0，`b` 必须放在 8 的倍数上，所以 `a` 后面有 **7 个字节**的填充；`c` 在 16，`d` 必须在 4 的倍数上，所以 `c` 后面又有 3 个字节填充。
- **结构体的总大小**必须是其中最大的对齐要求（这里是 8）的倍数，这样在结构体数组中，每个元素的 `double` 成员仍然是对齐的。`struct Good` 的成员只占到 14，所以末尾补了 2 个字节，凑成 16。

**按照成员的大小从大到小排列**，通常就能把填充减到最少。对于只有几个的结构体，这无关紧要；但如果一个结构体有上百万个实例（比如游戏中的粒子、数据库中的记录），节省三分之一的内存是很可观的。

这也提醒我们：**不要假设结构体的大小等于成员大小之和**，计算结构体数组需要的内存，或者读写二进制文件时，都要用 `sizeof`。

## 3. 结构体赋值是浅拷贝

结构体赋值会复制所有成员。但如果成员是一个**指针**，复制的只是指针的值（地址），而不是它指向的数据：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* name;                                 // 指向堆上的字符串
    int age;
} Person;

int main(void)
{
    Person a;
    a.name = malloc(16);
    strcpy(a.name, "Alice");
    a.age = 30;

    Person b = a;                               // 只复制了指针，没有复制字符串
    b.name[0] = 'M';                            // 想修改 b 的名字
    b.age = 25;
    printf("a: %s %d\n", a.name, a.age);
    printf("b: %s %d\n", b.name, b.age);
    printf("a.name == b.name ? %s\n", a.name == b.name ? "是，指向同一块内存" : "否");

    fflush(stdout);
    free(a.name);
    free(b.name);                               // 重复释放！
    return 0;
}
```

```
a: Mlice 30
b: Mlice 25
a.name == b.name ? 是，指向同一块内存
```

程序最后崩溃了（退出码 133）。

- `age` 是普通的 `int`，被独立地复制了，修改 `b.age` 不影响 `a`。
- `name` 是指针，`a.name` 和 `b.name` **指向同一块内存**。修改 `b` 的名字，`a` 的名字也变了。
- 两个结构体都以为自己「拥有」这块内存，各自释放一次，造成了上一篇讲的**重复释放**。AddressSanitizer 报告：`attempting double-free ... in main shallow.c:26`。

这种只复制指针本身的复制叫作**浅拷贝**。需要真正独立的副本时，要写一个函数，为指针成员重新分配内存并复制内容，叫作**深拷贝**。C++ 中的拷贝构造函数就是为了解决这个问题（《构造函数与析构函数》）。

## 4. 自引用结构体：链表节点

结构体的成员可以是**指向同类型结构体的指针**，这是链表、树等数据结构的基础：

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    int value;
    struct Node* next;                          // 指向同类型的下一个节点
} Node;

Node* pushFront(Node* head, int value)
{
    Node* n = malloc(sizeof(Node));
    if (n == NULL) {
        return head;
    }
    n->value = value;
    n->next = head;
    return n;                                   // 新节点成为新的头
}

void freeList(Node* head)
{
    while (head != NULL) {
        Node* next = head->next;                // 先保存下一个，再释放当前
        free(head);
        head = next;
    }
}

int main(void)
{
    Node* head = NULL;
    for (int i = 1; i <= 4; ++i) {
        head = pushFront(head, i * 10);
    }
    for (Node* p = head; p != NULL; p = p->next) {
        printf("%d -> ", p->value);
    }
    printf("NULL\n");
    freeList(head);
    return 0;
}
```

```
40 -> 30 -> 20 -> 10 -> NULL
```

用 `leaks` 检查：`0 leaks for 0 total leaked bytes.`

- 结构体中不能包含**它自己**（那样大小就是无穷大了），但可以包含**指向自己类型的指针**（只占 8 字节）。
- 在结构体内部，`typedef` 的别名 `Node` 还没有定义完成，所以成员要写成 `struct Node*`，结构体也要有名字 `struct Node`。
- `freeList` 中必须**先保存 `next`，再释放当前节点**。如果先 `free(head)` 再读 `head->next`，就是释放后使用。

链表的插入、删除、反转等操作，会在数据结构专栏中详细讲解。

## 5. 枚举

**枚举**（enum）定义一组有名字的整数常量：

```c
#include <stdio.h>

enum Color { RED, GREEN, BLUE };                // 0, 1, 2
enum Status { OK = 0, NOT_FOUND = 404, SERVER_ERROR = 500 };

const char* colorName(enum Color c)
{
    switch (c) {
    case RED:   return "红";
    case GREEN: return "绿";
    case BLUE:  return "蓝";
    }
    return "未知";
}

int main(void)
{
    enum Color c = GREEN;
    printf("GREEN = %d，名字 %s\n", c, colorName(c));
    printf("NOT_FOUND = %d\n", NOT_FOUND);
    c = 7;                                      // C 语言不阻止这样做
    printf("c = 7 时: %s\n", colorName(c));
    return 0;
}
```

```
GREEN = 1，名字 绿
NOT_FOUND = 404
c = 7 时: 未知
```

- 不指定值时，从 0 开始依次加 1；也可以给每个名字指定值。
- 用 `RED`、`NOT_FOUND` 代替 `0`、`404` 这样的「魔法数字」，代码的含义一目了然。
- 在 `switch` 中使用枚举时，如果**漏掉了某个值**，`-Wall` 会给出警告，这是枚举的一大好处：

  ```
  warning: enumeration value 'BLUE' not handled in switch [-Wswitch]
  ```

- C 语言的枚举本质上就是 `int`，**不做类型检查**：`c = 7` 能编译通过，即使 7 不是任何一个合法的颜色。所以 `colorName` 在 `switch` 后面还要处理「未知」的情况。C++11 的 `enum class` 修正了这个问题（《enum class 与 static_assert》）。

## 6. 联合

**联合**（union）的语法和结构体一样，但**所有成员共用同一块内存**，大小等于最大的成员：

```c
union Value {                                   // 所有成员共用同一块内存
    int i;
    float f;
    unsigned char bytes[4];
};

union Value v;
printf("sizeof(union Value) = %zu\n", sizeof(union Value));
v.f = 1.0f;
printf("v.f = 1.0 的 4 个字节: %02x %02x %02x %02x，当作 int 读取: %d\n",
       v.bytes[0], v.bytes[1], v.bytes[2], v.bytes[3], v.i);
```

```
sizeof(union Value) = 4
v.f = 1.0 的 4 个字节: 00 00 80 3f，当作 int 读取: 1065353216
```

写入 `f` 之后，通过 `bytes` 可以看到浮点数 1.0 在内存中的二进制表示（`0x3f800000`，因为这台机器是小端序，低位字节在前），通过 `i` 读取则把同样的 4 个字节当作整数解释。

联合最常见的用途是**带标记的联合**：一个值可能是几种类型之一，用一个枚举记录当前是哪一种：

```c
typedef enum { INT_VALUE, FLOAT_VALUE, TEXT_VALUE } Kind;

typedef struct {
    Kind kind;                                  // 标记当前存放的是哪种数据
    union {
        int i;
        double d;
        char text[16];
    } as;
} Cell;

void printCell(const Cell* c)
{
    switch (c->kind) {
    case INT_VALUE:   printf("整数 %d\n", c->as.i); break;
    case FLOAT_VALUE: printf("小数 %.2f\n", c->as.d); break;
    case TEXT_VALUE:  printf("文本 \"%s\"\n", c->as.text); break;
    }
}

Cell cells[3] = {
    {INT_VALUE, {.i = 42}},
    {FLOAT_VALUE, {.d = 3.14}},
    {TEXT_VALUE, {.text = "hello"}},
};
```

```
整数 42
小数 3.14
文本 "hello"
sizeof(Cell) = 24
```

一个电子表格的单元格可能是整数、小数或文本，用联合只需要按最大的成员（16 字节的 `text`）分配空间，而不是三者之和。`kind` 必须和实际存放的数据保持一致，读取不是最后写入的那个成员，就会得到错误的结果。C++17 的 `std::variant` 是类型安全的带标记联合（《std::variant：类型安全的联合体》）。

## 7. 常见错误

1. **结构体定义末尾漏了分号**：编译错误，而且错误信息常常指向下一行。
2. **用 `.` 访问指针的成员，或者用 `->` 访问变量的成员**：`p.x` 应该是 `p->x`。
3. **按值传递大结构体**：每次调用都复制整个结构体。
4. **用 `==` 比较结构体**：编译错误，要逐个比较成员。
5. **假设结构体大小等于成员大小之和**：有填充，用 `sizeof`。
6. **结构体赋值后以为指针成员也被复制了**：浅拷贝，两者共享数据，最后重复释放。
7. **释放链表时先 `free` 再读 `next`**：释放后使用。
8. **以为枚举变量只能取定义过的值**：C 语言不检查。
9. **读取联合中不是最后写入的成员**：得到的是对同一块内存的另一种解释。

## 8. 小结

- 结构体把不同类型的数据组合在一起；变量用 `.`，指针用 `->` 访问成员；`typedef` 简化类型名
- 结构体可以整体赋值，但不能用 `==` 比较；作为参数时传指针
- 编译器会为对齐插入填充，结构体大小可能大于成员之和；成员从大到小排列可以减少填充（实测 24 → 16 字节）
- 结构体赋值是浅拷贝，指针成员只复制地址
- 结构体可以包含指向自身类型的指针，这是链表和树的基础
- 枚举定义有名字的整数常量，配合 `switch` 可以检查遗漏，但 C 语言不检查取值范围
- 联合的成员共用内存；带标记的联合用一个枚举记录当前存放的类型

## 9. 练习

1. 定义结构体 `Rect`，包含左上角的 `Point topLeft` 以及 `width`、`height`。写函数 `double area(const Rect* r)` 和 `int contains(const Rect* r, Point p)`（判断点是否在矩形内，这里假设 y 轴向下）。
2. 定义 `Student` 结构体（姓名和成绩），用 `qsort` 把一个 `Student` 数组按成绩从低到高排序。
3. 在 64 位系统上，`struct A { char a; int b; char c; };` 的大小是多少？调整成员顺序，最小可以是多少？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   typedef struct { double x, y; } Point;
   typedef struct { Point topLeft; double width, height; } Rect;

   double area(const Rect* r) { return r->width * r->height; }

   int contains(const Rect* r, Point p)
   {
       return p.x >= r->topLeft.x && p.x <= r->topLeft.x + r->width &&
              p.y >= r->topLeft.y && p.y <= r->topLeft.y + r->height;
   }
   ```

   对 `Rect r = {{0, 0}, 4, 3};`，面积是 12.0，点 (1, 2) 在矩形内，点 (5, 1) 在矩形外。注意嵌套结构体的访问方式 `r->topLeft.x`。`Point` 只有 16 字节，按值传递也可以。

2. 参考代码：

   ```c
   typedef struct { char name[16]; int score; } Student;

   int byScore(const void* a, const void* b)
   {
       const Student* s1 = a;
       const Student* s2 = b;
       return (s1->score > s2->score) - (s1->score < s2->score);
   }

   qsort(s, 3, sizeof(Student), byScore);
   ```

   对 `{"Tom", 85}, {"Amy", 92}, {"Bob", 78}`，结果是 Bob 78、Tom 85、Amy 92。注意 `qsort` 的第三个参数是 `sizeof(Student)`，它已经包含了填充。

3. 12 字节：`a` 在 0，后面填充 3 字节，`b` 在 4，`c` 在 8，末尾填充 3 字节凑成 4 的倍数。改为 `struct B { int b; char a; char c; };`，`b` 在 0，`a` 在 4，`c` 在 5，末尾填充 2 字节，一共 8 字节。实测 `sizeof` 分别是 12 和 8。

</details>
