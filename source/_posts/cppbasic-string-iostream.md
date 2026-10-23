---
title: 字符串与 IO 流
date: 2026-10-23 13:20:00
updated: 2026-10-23 13:20:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - string
  - IO 流
order: 14
description: std::string 和 IO 流是每个 C++ 程序都会用到的工具。本文介绍 string 的常用操作，复现 cin 与 getline 混用、输入错误后流失效两个常见的输入陷阱，讲解格式化输出、文件读写和 stringstream，并实测 std::endl 比 '\n' 慢 40 多倍的原因。
disableNunjucks: true
---

> **本文目标**：熟练使用 `std::string` 的常用操作；理解输入流的工作方式，处理好 `>>` 与 `getline` 混用和输入错误；会用 `<iomanip>` 格式化输出；会用 `fstream` 读写文件，用 `stringstream` 解析和拼接字符串；知道什么时候不该用 `std::endl`。
>
> **前置知识**：《从 C 到 C++》、《STL 容器》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. std::string

《从 C 到 C++》中已经用过 `std::string`。和 C 语言的字符数组相比，它**自动管理内存**，不用担心缓冲区溢出，也不用手动 `strcpy`、`strcat`。它也是一个 STL 容器，支持迭代器和上一篇的所有算法。

```cpp
#include <iostream>
#include <string>

int main()
{
    std::string s = "Hello";
    s += ", World";                                     // 拼接
    std::cout << s << "，长度 " << s.size() << '\n';
    std::cout << "s[0] = " << s[0] << "，back = " << s.back() << '\n';

    std::cout << "substr(7, 5) = " << s.substr(7, 5) << '\n';     // 从下标 7 开始取 5 个字符
    std::size_t pos = s.find("World");
    std::cout << "find(\"World\") = " << pos << '\n';
    if (s.find("C++") == std::string::npos) {
        std::cout << "find(\"C++\") = npos（没有找到）\n";
    }

    s.replace(pos, 5, "C++");                           // 把 World 替换为 C++
    std::cout << "replace 之后: " << s << '\n';
    s.insert(0, ">> ");
    s.erase(s.size() - 1);                              // 删除最后一个字符
    std::cout << "insert / erase 之后: " << s << '\n';

    std::string a = "apple", b = "banana";
    std::cout << std::boolalpha << "a < b: " << (a < b) << "，a == \"apple\": " << (a == "apple") << '\n';

    int n = std::stoi("42");
    double d = std::stod("3.14");
    std::string t = std::to_string(n * 2) + " / " + std::to_string(d);
    std::cout << "数字转换: " << t << '\n';

    std::string cn = "你好";
    std::cout << "\"你好\".size() = " << cn.size() << '\n';
    return 0;
}
```

运行结果：

```
Hello, World，长度 12
s[0] = H，back = d
substr(7, 5) = World
find("World") = 7
find("C++") = npos（没有找到）
replace 之后: Hello, C++
insert / erase 之后: >> Hello, C+
a < b: true，a == "apple": true
数字转换: 84 / 3.140000
"你好".size() = 6
```

| 操作 | 说明 |
|---|---|
| `s.size()` / `s.length()` | 字符个数（准确地说是字节数） |
| `s[i]` / `s.at(i)` | 访问字符，`at` 检查越界 |
| `+` / `+=` | 拼接 |
| `==`、`<` 等 | 按字典顺序比较内容 |
| `s.substr(pos, len)` | 取子串 |
| `s.find(str)` | 查找，返回下标；找不到返回 **`std::string::npos`** |
| `s.replace(pos, len, str)` | 替换 |
| `s.insert(pos, str)` / `s.erase(pos, len)` | 插入、删除 |
| `std::stoi` / `std::stod` / `std::to_string` | 字符串和数字之间的转换 |
| `s.c_str()` | 得到 C 风格字符串 `const char*`，用于调用 C 语言的函数 |

几个要注意的地方：

- **`find` 找不到时返回 `npos`**，不是 -1，也不是 0。判断时要和 `std::string::npos` 比较。
- **`size()` 返回的是字节数**。`"你好"` 在 UTF-8 编码中占 6 个字节，所以 `size()` 是 6，`s[0]` 也只是「你」字的第一个字节。`std::string` 不理解字符编码，处理中文时要格外小心。
- `std::to_string(3.14)` 固定输出 6 位小数，得到 `"3.140000"`。需要控制格式时，用第 4 节的 `ostringstream`。
- **两个字符串字面量不能直接相加**：

  ```cpp
  std::string s = "Hello" + ", World";
  ```

  ```
  error: invalid operands to binary expression ('const char[6]' and 'const char[8]')
  ```

  `"Hello"` 和 `", World"` 都是字符数组，不是 `std::string`。只要 `+` 的两边有一个是 `std::string` 就可以了，见本文的练习 3。

> 只需要读取一个字符串、不需要修改也不需要保存时，函数参数可以用 C++17 的 `std::string_view`，避免拷贝，详见 C++17 章节的《std::string_view 与 std::any》。

## 2. 输入流的两个陷阱

`std::cin >> x` 的工作方式是：**跳过前面的空白字符（空格、制表符、换行），然后读取尽可能多的、符合 `x` 类型的字符，遇到不符合的字符就停下**，剩下的字符留在输入流中，等待下一次读取。理解了这一点，就能理解下面两个陷阱。

### 2.1 `>>` 和 `getline` 混用

`>>` 遇到空格就停止，要读取一整行（比如带空格的姓名），需要用 **`std::getline(std::cin, s)`**。但是两者混用时：

```cpp
#include <iostream>
#include <string>

int main()
{
    int age;
    std::string name;
    std::cout << "年龄: ";
    std::cin >> age;
    std::cout << "姓名: ";
    std::getline(std::cin, name);          // 读到的是 age 后面剩下的换行符
    std::cout << "\n[age = " << age << ", name = \"" << name << "\"]\n";
    return 0;
}
```

输入 `18`、回车、`Tom Smith`、回车（这里用 `printf '18\nTom Smith\n' | ./getline` 模拟）：

```
年龄: 姓名: 
[age = 18, name = ""]
```

程序根本没有等待输入姓名，`name` 是空的。原因是：`cin >> age` 读取了 `18`，**把后面的换行符留在了输入流中**；`getline` 读取到换行符为止，于是立即读到了一个空行。

解决方法是在 `getline` 之前，把这一行剩下的内容（包括换行符）丢弃：

```cpp
#include <limits>

std::cin >> age;
std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');   // 丢弃这一行剩下的内容
std::getline(std::cin, name);
```

```
年龄: 姓名: 
[age = 18, name = "Tom Smith"]
```

`ignore(n, '\n')` 的意思是「最多丢弃 n 个字符，遇到 `'\n'` 为止（`'\n'` 也被丢弃）」，这里的 n 取了最大值，表示不限制个数。

### 2.2 输入错误后流失效

如果用户在需要整数的地方输入了字母：

```cpp
int x = -1;
std::cin >> x;
std::cout << "x = " << x << ", fail = " << std::cin.fail() << '\n';
int y = -1;
std::cin >> y;                          // 流处于错误状态，什么都不读
std::cout << "y = " << y << '\n';
```

输入 `abc 5`：

```
x = 0, fail = 1
y = -1
```

- 读取 `x` 失败，`x` 被设置为 0（C++11 起的规定），流进入**失败状态**，`fail()` 返回 `true`。
- 失败状态的流**会忽略之后所有的读取操作**，所以即使后面有一个 5，`y` 也没有被读取，仍然是 -1。错误的输入 `abc` 也还留在流中。

正确的处理方式：**检查每次读取是否成功**；失败时先 `clear()` 清除错误状态，再 `ignore` 丢弃错误的输入：

```cpp
#include <iostream>
#include <limits>

int main()
{
    int x;
    while (true) {
        std::cout << "请输入一个整数: ";
        if (std::cin >> x) {
            break;                                         // 读取成功
        }
        if (std::cin.eof()) {
            std::cout << "\n输入结束\n";
            return 1;
        }
        std::cout << "\n输入无效，请重试\n";
        std::cin.clear();                                  // 清除错误状态
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');   // 丢弃错误的输入
    }
    std::cout << "\n读到了 " << x << '\n';
    return 0;
}
```

输入两行 `abc` 和 `12x`：

```
请输入一个整数: 
输入无效，请重试
请输入一个整数: 
读到了 12
```

注意第二行的 `12x` 被成功读取为 12，`x` 留在了流中。这正是 `>>` 「读取尽可能多的符合类型的字符」的行为。如果需要严格检查整行，可以先用 `getline` 读取一整行，再用 `std::stoi` 转换并检查是否用完了所有字符。

另外，**`if (std::cin >> x)` 和 `while (std::cin >> x)` 是读取输入的标准写法**：`>>` 返回流本身，流在布尔上下文中表示「上一次操作是否成功」。读到文件末尾（`eof()`）或者出错时，循环自然结束。

## 3. 格式化输出

头文件 **`<iomanip>`** 提供了一组「操纵符」，用 `<<` 插入流中来控制格式：

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    double pi = 3.14159265;
    std::cout << pi << '\n';                                       // 默认 6 位有效数字
    std::cout << std::setprecision(3) << pi << '\n';               // 3 位有效数字
    std::cout << std::fixed << std::setprecision(2) << pi << '\n'; // 小数点后 2 位
    std::cout << '[' << std::setw(8) << 42 << "]\n";               // 宽度 8，默认右对齐
    std::cout << '[' << std::left << std::setw(8) << 42 << "]\n";  // 左对齐
    std::cout << std::right << std::setfill('0') << std::setw(5) << 42 << '\n';  // 用 0 填充
    std::cout << std::hex << 255 << ' ' << std::dec << 255 << '\n';
    std::cout << std::setfill(' ');

    std::cout << "\n" << std::left << std::setw(8) << "名称" << std::right << std::setw(8) << "价格" << '\n';
    std::cout << std::left << std::setw(8) << "apple" << std::right << std::setw(8) << 3.5 << '\n';
    std::cout << std::left << std::setw(8) << "banana" << std::right << std::setw(8) << 12.25 << '\n';
    return 0;
}
```

运行结果：

```
3.14159
3.14
3.14
[      42]
[42      ]
00042
ff 255

名称    价格
apple       3.50
banana     12.25
```

| 操纵符 | 作用 | 持续时间 |
|---|---|---|
| `setprecision(n)` | 有效数字位数；和 `fixed` 一起使用时表示小数位数 | 一直有效 |
| `fixed` | 定点表示法（不用科学计数法） | 一直有效 |
| `setw(n)` | 下一项输出的最小宽度 | **只对下一项有效** |
| `left` / `right` | 左对齐 / 右对齐 | 一直有效 |
| `setfill(c)` | 填充字符 | 一直有效 |
| `hex` / `dec` / `oct` | 整数的进制 | 一直有效 |

- 除了 `setw`，其他设置**会一直保持**，影响后面所有的输出。上面的 `00042` 之后，如果不把填充字符改回空格，后面的表格就会被 0 填满。
- 最后的表格中，表头没有对齐。因为 **`setw` 按字节计算宽度**：「名称」在 UTF-8 中占 6 个字节，但在屏幕上只占 4 个英文字符的宽度。输出中文表格时要注意这一点。

> C++20 的 `std::format` 提供了类似 Python 的格式化语法，比如 `std::format("{:>8.2f}", pi)`，比操纵符直观得多，详见 C++20 章节的《C++20 常用标准库：span、format、jthread 等》。

## 4. 文件流和字符串流

`std::cin`、`std::cout` 是连接到控制台的流。C++ 还有连接到**文件**和**字符串**的流，它们的用法完全一样，因为它们都派生自同样的基类（`std::istream` / `std::ostream`），这正是《继承》和《多态与虚函数》的应用。

### 4.1 读写文件

```cpp
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    {
        std::ofstream out("scores.txt");                  // 打开文件用于写入（覆盖原内容）
        if (!out) {
            std::cerr << "无法创建文件\n";
            return 1;
        }
        out << "Alice 90\n";
        out << "Bob 85\n";
        out << "Carol 77\n";
    }                                                     // out 析构时自动关闭文件

    std::ifstream in("scores.txt");
    if (!in) {
        std::cerr << "无法打开文件\n";
        return 1;
    }
    std::string name;
    int score, total = 0, count = 0;
    while (in >> name >> score) {                         // 读取成功才进入循环
        std::cout << name << ": " << score << '\n';
        total += score;
        ++count;
    }
    std::cout << "平均分: " << static_cast<double>(total) / count << '\n';

    std::ifstream missing("no_such_file.txt");
    if (!missing) {
        std::cerr << "打开 no_such_file.txt 失败\n";
    }
    return 0;
}
```

运行结果：

```
Alice: 90
Bob: 85
Carol: 77
平均分: 84
打开 no_such_file.txt 失败
```

- **`std::ofstream`** 用于写文件，**`std::ifstream`** 用于读文件，头文件 `<fstream>`。默认情况下 `ofstream` 会清空原文件；要追加内容，用 `std::ofstream out("log.txt", std::ios::app);`。
- **打开之后一定要检查** `if (!in)`。文件不存在或者没有权限时，打开会失败，但**不会抛出异常**，后面的读取都会静默失败。
- **不需要手动 `close()`**。文件流是 RAII 对象，析构时自动关闭文件。上面用一对花括号限定了 `out` 的作用域，保证在读取之前文件已经关闭（内容已经写入磁盘）。
- 错误信息输出到 **`std::cerr`**（标准错误），而不是 `std::cout`。这样即使用户把程序的输出重定向到文件，错误信息仍然会显示在屏幕上。

### 4.2 stringstream：解析和拼接字符串

头文件 `<sstream>` 中的 **`std::istringstream`** 把一个字符串当作输入流，**`std::ostringstream`** 把输出写到一个字符串中：

```cpp
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    std::string csv = "10,20,30,40";
    std::istringstream in(csv);
    std::string field;
    std::vector<int> numbers;
    while (std::getline(in, field, ',')) {                // 以逗号为分隔符读取
        numbers.push_back(std::stoi(field));
    }
    std::cout << "解析出 " << numbers.size() << " 个数，第三个是 " << numbers[2] << '\n';

    std::ostringstream out;                               // 像写 cout 一样拼接字符串
    out << "共 " << numbers.size() << " 项，平均值 " << (10 + 20 + 30 + 40) / 4.0;
    std::string report = out.str();
    std::cout << report << '\n';
    return 0;
}
```

运行结果：

```
解析出 4 个数，第三个是 30
共 4 项，平均值 25
```

- `std::getline` 的第三个参数可以指定分隔符，默认是 `'\n'`。用它可以方便地拆分字符串。
- `ostringstream` 可以使用第 3 节所有的格式化操纵符，最后用 `str()` 取出结果。需要拼接数字和文字、又要控制格式时，它比 `std::to_string` 灵活。
- 《STL 容器》中统计单词的例子，就是用 `istringstream` 配合 `>>` 按空白拆分单词的。

## 5. std::endl 与 '\n'

很多教程用 `std::endl` 换行。它其实做了两件事：**输出换行符，并刷新（flush）缓冲区**。输出流有一个缓冲区，数据先放在缓冲区中，攒够了再一次性写入文件或终端，这样效率高得多。`std::endl` 每次都强制写入，缓冲区就失去了作用。实测写入 100 万行：

```cpp
double t1 = timeIt([&] {
    std::ofstream out("endl.txt");
    for (int i = 0; i < n; ++i) out << i << std::endl;     // 每行都刷新缓冲区
});
double t2 = timeIt([&] {
    std::ofstream out("newline.txt");
    for (int i = 0; i < n; ++i) out << i << '\n';          // 只换行，缓冲区满了才写入
});
```

```
写入 1000000 行:
  std::endl: 1188.67 ms
  '\n':      27.1968 ms
```

`std::endl` 慢了 **40 多倍**。所以本系列的代码一直用 `'\n'` 换行。只有在确实需要立即看到输出时（比如程序可能崩溃，希望在崩溃前把日志写出去）才使用 `std::endl` 或 `std::flush`。`std::cerr` 默认不缓冲，写入后立即可见，也适合这种情况。

## 6. 常见错误

1. **把 `find` 的返回值和 -1 或 0 比较**：找不到时返回的是 `std::string::npos`。
2. **用 `size()` 计算中文字符的个数**：得到的是字节数。
3. **两个字符串字面量直接相加**：编译错误，至少有一边要是 `std::string`。
4. **`cin >> x` 之后直接 `getline`**：读到一个空行。先 `ignore` 掉剩下的换行符。
5. **不检查输入是否成功**：输入错误后流进入失败状态，之后的读取全部被跳过，变量保持原值。
6. **打开文件后不检查**：文件打开失败不会抛出异常，后面的读写都静默失败。
7. **以为 `setw` 一直有效，或者以为 `fixed`、`setfill` 只对下一项有效**：恰好相反。
8. **大量输出时使用 `std::endl`**：每次都刷新缓冲区，慢几十倍。

## 7. 小结

- `std::string` 自动管理内存，支持拼接、比较、查找、替换等操作；`find` 找不到时返回 `npos`；`size()` 是字节数
- `>>` 跳过空白、读到不符合类型的字符为止，剩下的字符留在流中
- `>>` 和 `getline` 混用前要 `ignore` 掉剩下的换行符；输入错误后要 `clear()` 再 `ignore`
- `while (in >> x)` 是读取输入的标准写法
- `<iomanip>` 的操纵符控制格式，`setw` 只对下一项有效，其他的一直有效；`setw` 按字节计算宽度
- `ifstream` / `ofstream` 读写文件，打开后要检查，析构时自动关闭
- `istringstream` 用于解析字符串，`ostringstream` 用于拼接字符串
- 实测 `std::endl` 比 `'\n'` 慢 40 多倍，换行用 `'\n'`

## 8. 练习

1. 用 `istringstream` 和 `getline` 写一个函数 `std::vector<std::string> split(const std::string& s, char delim)`，用 `split("a,b,,c", ',')` 测试，结果应该包含几个元素？
2. 写一个简单的 `wc` 程序：从命令行参数中读取一个文件名，统计文件的行数、单词数和字节数。
3. 修改 `std::string s = "Hello" + ", World";`，让它能够编译。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::vector<std::string> split(const std::string& s, char delim)
   {
       std::vector<std::string> parts;
       std::istringstream in(s);
       std::string field;
       while (std::getline(in, field, delim)) {
           parts.push_back(field);
       }
       return parts;
   }
   ```

   结果是 4 个元素：`[a][b][][c]`，两个相邻的逗号之间是一个空字符串。

2. 参考代码：

   ```cpp
   int main(int argc, char* argv[])
   {
       if (argc < 2) {
           std::cerr << "用法: " << argv[0] << " 文件名\n";
           return 1;
       }
       std::ifstream in(argv[1]);
       if (!in) {
           std::cerr << "无法打开 " << argv[1] << '\n';
           return 1;
       }
       std::size_t lines = 0, words = 0, chars = 0;
       std::string line, word;
       while (std::getline(in, line)) {
           ++lines;
           chars += line.size() + 1;                 // +1 是被 getline 去掉的换行符
           std::istringstream ls(line);
           while (ls >> word) ++words;
       }
       std::cout << lines << " 行，" << words << " 个单词，" << chars << " 个字节\n";
   }
   ```

   对第 4.1 节生成的 `scores.txt` 运行，输出 `3 行，6 个单词，25 个字节`，和系统的 `wc scores.txt` 结果一致。（如果文件的最后一行没有换行符，字节数会多算 1。）

3. 让其中一边成为 `std::string`：

   ```cpp
   std::string s1 = std::string("Hello") + ", World";

   using namespace std::string_literals;
   std::string s2 = "Hello"s + ", World";        // C++14 的字符串字面量后缀 s
   ```

   如果只是两个字面量拼在一起，也可以直接写成 `"Hello" ", World"`，相邻的字符串字面量会在编译时自动连接。

</details>
