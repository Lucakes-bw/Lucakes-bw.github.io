---
title: std::filesystem：跨平台的文件与目录操作
date: 2026-10-26 09:00:00
updated: 2026-10-26 09:00:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 标准库
  - 文件
order: 8
description: C++17 终于把文件系统操作纳入了标准库：拼接和拆解路径、查询文件属性、创建删除目录、递归遍历、复制重命名，同一份代码在 Linux、macOS、Windows 上都能运行。本文最后用它写一个按扩展名统计目录的小工具。
disableNunjucks: true
---

> **本文目标**：会用 `fs::path` 拼接、拆解路径；会查询文件是否存在、类型和大小；会创建、复制、重命名、删除文件和目录；会用 `directory_iterator` 和 `recursive_directory_iterator` 遍历目录；会用异常和 `error_code` 两种方式处理错误。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，需要包含头文件 `<filesystem>`。GCC 8 需要额外链接 `-lstdc++fs`，GCC 9 起不再需要。

## 1. 为什么需要 std::filesystem

C++17 之前，标准库只能读写**文件的内容**（`fstream`），却不能处理**文件系统本身**：判断文件是否存在、创建目录、列出目录中的文件……这些都要依赖操作系统的 API：

| 操作 | Linux / macOS（POSIX） | Windows |
| --- | --- | --- |
| 查询文件信息 | `stat` | `GetFileAttributesEx` |
| 创建目录 | `mkdir` | `CreateDirectoryW` |
| 遍历目录 | `opendir` / `readdir` | `FindFirstFileW` / `FindNextFileW` |

想写跨平台的程序，就得用 `#ifdef` 写两套代码，或者依赖 Boost.Filesystem。C++17 把 Boost.Filesystem 几乎原样纳入了标准库，成为 **`std::filesystem`**。

通常会给它起一个短别名：

```cpp
namespace fs = std::filesystem;
```

## 2. fs::path：路径

`fs::path` 表示一个路径。它只是一个字符串的包装，**不要求路径真实存在**，所有操作都只是字符串处理。

```cpp
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    fs::path p = "/home/lucky/projects/blog/source/_posts/cpp17-optional.md";
    std::cout << "完整路径    : " << p << '\n'
              << "filename()  : " << p.filename() << '\n'
              << "stem()      : " << p.stem() << '\n'
              << "extension() : " << p.extension() << '\n'
              << "parent_path : " << p.parent_path() << '\n'
              << "root_path   : " << p.root_path() << '\n';

    fs::path dir = "projects";
    fs::path full = dir / "blog" / "README.md";          // 用 / 拼接路径，自动处理分隔符
    std::cout << "拼接        : " << full << '\n';

    fs::path img = "photo.JPG";
    img.replace_extension(".png");
    std::cout << "替换扩展名  : " << img << '\n';

    std::cout << "相对路径    : " << fs::path("a/b/../c/./d.txt").lexically_normal() << '\n';
    std::cout << "is_absolute : " << std::boolalpha << p.is_absolute() << ", " << full.is_absolute() << '\n';

    for (const auto& part : fs::path("usr/local/bin"))   // 逐段遍历
        std::cout << '[' << part.string() << ']';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
完整路径    : "/home/lucky/projects/blog/source/_posts/cpp17-optional.md"
filename()  : "cpp17-optional.md"
stem()      : "cpp17-optional"
extension() : ".md"
parent_path : "/home/lucky/projects/blog/source/_posts"
root_path   : "/"
拼接        : "projects/blog/README.md"
替换扩展名  : "photo.png"
相对路径    : "a/c/d.txt"
is_absolute : true, false
[usr][local][bin]
```

| 成员 | 作用 | 例子（`/a/b/photo.tar.gz`） |
| --- | --- | --- |
| `filename()` | 文件名 | `photo.tar.gz` |
| `stem()` | 去掉最后一个扩展名的文件名 | `photo.tar` |
| `extension()` | 最后一个扩展名（包含点） | `.gz` |
| `parent_path()` | 上一级目录 | `/a/b` |
| `/` 运算符 | 拼接路径，自动添加分隔符 | `fs::path("a") / "b"` → `a/b` |
| `replace_extension()` | 替换扩展名 | |
| `lexically_normal()` | 消除 `.`、`..` 和多余的分隔符（纯字符串处理） | `a/b/../c` → `a/c` |
| `string()` | 转换成 `std::string` | |

注意两点：

- **用 `<<` 输出 `path` 时会自动加上双引号**，因为路径中可能包含空格。不想要引号时，输出 `p.string()`。
- **用 `/` 拼接，不要用字符串的 `+`**：`/` 会在需要时自动加上分隔符，在 Windows 上使用 `\`，代码不用关心平台差异。

## 3. 文件和目录操作

下面的程序在当前目录下创建一个小项目，演示各种操作，最后把它删除干净：

```cpp
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void writeFile(const fs::path& p, const std::string& content)
{
    std::ofstream(p) << content;                           // ofstream 可以直接接收 path
}

int main()
{
    fs::path root = "demo_project";
    fs::remove_all(root);                                  // 清理上次运行留下的目录

    // 1. 创建多级目录
    fs::create_directories(root / "src" / "utils");
    fs::create_directories(root / "docs");
    writeFile(root / "src" / "main.cpp", "int main() { return 0; }\n");
    writeFile(root / "src" / "utils" / "math.cpp", "int add(int a, int b) { return a + b; }\n");
    writeFile(root / "docs" / "README.md", "# Demo\n");
    writeFile(root / "notes.txt", "hello\n");

    // 2. 查询
    fs::path f = root / "src" / "main.cpp";
    std::cout << f << " 存在: " << fs::exists(f) << ", 是文件: " << fs::is_regular_file(f)
              << ", 大小: " << fs::file_size(f) << " 字节\n";
    std::cout << root / "docs" << " 是目录: " << fs::is_directory(root / "docs") << '\n';

    // 3. 遍历当前层（顺序由系统决定，这里排序后输出）
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(root))
        names.push_back(entry.path().filename().string() + (entry.is_directory() ? "/" : ""));
    std::sort(names.begin(), names.end());
    std::cout << "顶层内容:";
    for (const auto& n : names) std::cout << ' ' << n;
    std::cout << '\n';

    // 4. 递归遍历，统计所有 .cpp 文件
    std::vector<std::string> sources;
    std::uintmax_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
            sources.push_back(fs::relative(entry.path(), root).string());
            total += entry.file_size();
        }
    }
    std::sort(sources.begin(), sources.end());
    std::cout << ".cpp 文件:";
    for (const auto& s : sources) std::cout << ' ' << s;
    std::cout << "，共 " << total << " 字节\n";

    // 5. 复制、重命名、删除
    fs::copy_file(root / "notes.txt", root / "notes_backup.txt");
    fs::rename(root / "notes.txt", root / "docs" / "notes.txt");
    fs::copy(root / "src", root / "src_copy", fs::copy_options::recursive);
    std::cout << "复制后 src_copy/utils/math.cpp 存在: " << fs::exists(root / "src_copy" / "utils" / "math.cpp") << '\n';
    auto removed = fs::remove_all(root / "src_copy");
    std::cout << "remove_all 删除了 " << removed << " 个文件和目录\n";

    fs::remove_all(root);
    std::cout << "清理完成，" << root << " 存在: " << fs::exists(root) << '\n';
    return 0;
}
```

运行结果：

```
"demo_project/src/main.cpp" 存在: 1, 是文件: 1, 大小: 25 字节
"demo_project/docs" 是目录: 1
顶层内容: docs/ notes.txt src/
.cpp 文件: src/main.cpp src/utils/math.cpp，共 65 字节
复制后 src_copy/utils/math.cpp 存在: 1
remove_all 删除了 4 个文件和目录
清理完成，"demo_project" 存在: 0
```

常用函数一览：

| 类别 | 函数 |
| --- | --- |
| 查询 | `exists`、`is_regular_file`、`is_directory`、`is_symlink`、`file_size`、`last_write_time`、`space`（磁盘空间） |
| 创建 | `create_directory`（只创建一级）、`create_directories`（自动创建所有不存在的上级目录） |
| 复制 | `copy_file`（单个文件）、`copy`（文件或目录，配合 `copy_options::recursive` 递归复制） |
| 移动 | `rename`（重命名或移动） |
| 删除 | `remove`（文件或空目录）、`remove_all`（递归删除，返回删除的数量） |
| 路径 | `current_path`（当前工作目录）、`absolute`、`canonical`（解析符号链接后的真实路径）、`relative`（相对路径）、`temp_directory_path` |

- **`directory_iterator`** 只遍历当前这一层，**`recursive_directory_iterator`** 会递归进入所有子目录。遍历得到的是 `directory_entry`，它缓存了文件的部分属性，调用 `entry.is_directory()`、`entry.file_size()` 通常比再用路径查询一次更快。
- **遍历的顺序由操作系统决定，标准不保证任何顺序**。上面的例子先收集到 `vector` 中再排序，输出才是确定的。
- `remove_all(root / "src_copy")` 删除了 4 项：`src_copy`、`src_copy/main.cpp`、`src_copy/utils`、`src_copy/utils/math.cpp`。
- **`remove_all` 和 `rm -rf` 一样危险**，不会进回收站。传入的路径一定要确认无误，尤其是由用户输入或程序拼接出来的路径。

## 4. 错误处理

大多数文件系统函数都有两个版本：

```cpp
#include <filesystem>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;

int main()
{
    // 1. 默认：出错时抛出 filesystem_error
    try {
        auto size = fs::file_size("no_such_file.txt");
        std::cout << size << '\n';
    } catch (const fs::filesystem_error& e) {
        std::cout << "异常: " << e.what() << '\n'
                  << "  错误码: " << e.code().message() << '\n'
                  << "  path1: " << e.path1() << '\n';
    }

    // 2. 传入 error_code：出错时不抛异常，通过 ec 返回错误
    std::error_code ec;
    auto size = fs::file_size("no_such_file.txt", ec);
    if (ec)
        std::cout << "error_code 版本: " << ec.message() << "，返回值 = " << static_cast<long long>(size) << '\n';
    return 0;
}
```

运行结果：

```
异常: filesystem error: in file_size: No such file or directory ["no_such_file.txt"]
  错误码: No such file or directory
  path1: "no_such_file.txt"
error_code 版本: No such file or directory，返回值 = -1
```

- **不带 `error_code` 的版本**出错时抛出 `fs::filesystem_error`，其中包含错误码和出错的路径。
- **带 `std::error_code&` 参数的版本**不抛异常，出错时设置 `ec`，并返回一个表示失败的值（`file_size` 返回 `static_cast<uintmax_t>(-1)`）。

文件系统操作出错是**很常见的正常情况**：文件被别的程序删除了、没有权限、磁盘满了。在遍历大量文件、出错后要继续处理的场合，用 `error_code` 版本更合适。

> 文件系统是被所有程序共享的。`if (fs::exists(p)) fs::file_size(p);` 这样「先检查再操作」的写法并不安全：检查之后、操作之前，文件可能已经被其他程序删除了。可靠的做法是直接操作，然后处理错误。

## 5. 实战：目录统计工具

把上面的内容组合起来，写一个按扩展名统计目录中文件数量和大小的小工具：

```cpp
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// 一个小工具：按扩展名统计目录中的文件数量和总大小，并列出最大的 3 个文件
int main(int argc, char* argv[])
{
    fs::path dir = argc > 1 ? argv[1] : ".";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::cerr << dir << " 不是一个目录\n";
        return 1;
    }

    struct Stat { int count = 0; std::uintmax_t bytes = 0; };
    std::map<std::string, Stat> byExt;
    std::vector<std::pair<std::uintmax_t, fs::path>> files;

    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::uintmax_t size = it->file_size(ec);
        if (ec) { ec.clear(); continue; }
        std::string ext = it->path().extension().string();
        if (ext.empty()) ext = "(无扩展名)";
        byExt[ext].count++;
        byExt[ext].bytes += size;
        files.emplace_back(size, it->path());
    }

    for (const auto& [ext, st] : byExt)
        std::cout << ext << ": " << st.count << " 个文件, " << st.bytes << " 字节\n";

    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::cout << "最大的文件:\n";
    for (std::size_t i = 0; i < files.size() && i < 3; ++i)
        std::cout << "  " << files[i].first << " 字节  " << fs::relative(files[i].second, dir).string() << '\n';
    return 0;
}
```

用它统计本博客的主题目录 `themes/lucky-term`：

```
.css: 1 个文件, 22154 字节
.ejs: 15 个文件, 19866 字节
.js: 2 个文件, 14690 字节
最大的文件:
  22154 字节  source/css/style.css
  11688 字节  source/js/main.js
  4283 字节  layout/page.ejs
```

这个工具用到了：

- `skip_permission_denied`：遇到没有权限访问的目录时跳过，而不是报错。
- 全程使用 `error_code` 版本，`it.increment(ec)` 代替 `++it`，**单个文件出错不会中断整个遍历**。
- 上一篇的结构化绑定 `for (const auto& [ext, st] : byExt)`。

同样的代码，在 Windows 上编译后可以直接统计 `C:\Users\...` 下的目录，不需要修改。

## 6. 常见错误

1. **用 `+` 拼接路径**：`dir.string() + "file.txt"` 会少一个分隔符，用 `dir / "file.txt"`。
2. **以为目录遍历有固定顺序**：需要有序时自己排序。
3. **在遍历目录的同时删除或添加文件**：结果是未指定的，可能漏掉或重复。先收集路径，遍历结束后再操作。
4. **忽略 Windows 的编码问题**：Windows 上 `path` 内部使用 UTF-16，`p.string()` 会转换成当前系统的代码页，中文路径可能变成乱码。跨平台代码处理中文路径时，用 `p.u8string()` 得到 UTF-8 编码（C++20 中它的返回类型变成了 `std::u8string`）。
5. **把 `lexically_normal` 当作真实路径**：它只做字符串处理，不会解析符号链接。需要磁盘上的真实路径时用 `fs::canonical`（要求路径存在）或 `fs::weakly_canonical`。

## 7. 小结

- `namespace fs = std::filesystem;`，一份代码适用于所有平台
- `fs::path` 用 `/` 拼接，`filename`、`stem`、`extension`、`parent_path` 拆解；只是字符串处理，不要求路径存在
- 查询：`exists`、`is_directory`、`file_size`；创建：`create_directories`；复制：`copy_file`、`copy`；移动：`rename`；删除：`remove`、`remove_all`
- `directory_iterator` 遍历一层，`recursive_directory_iterator` 递归遍历，顺序不确定
- 默认出错抛 `filesystem_error`；传入 `std::error_code&` 则不抛异常
- 文件系统是共享的，「先检查再操作」并不可靠，直接操作并处理错误

## 8. 练习

1. 写一个函数，找出目录（包括子目录）中所有修改时间在最近 24 小时内的文件。提示：`fs::last_write_time` 返回 `fs::file_time_type`，可以和 `fs::file_time_type::clock::now()` 相减。
2. 写一个函数 `backup(const fs::path& file)`，把文件复制为同目录下的 `原名.bak`；如果 `.bak` 已经存在，就覆盖它。（提示：`fs::copy_options::overwrite_existing`）
3. 下面的代码想删除目录中所有的 `.tmp` 文件，有什么问题？

   ```cpp
   for (const auto& e : fs::recursive_directory_iterator(dir))
       if (e.path().extension() == ".tmp")
           fs::remove(e.path());
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::vector<fs::path> recentFiles(const fs::path& dir)
   {
       std::vector<fs::path> result;
       auto now = fs::file_time_type::clock::now();
       for (const auto& e : fs::recursive_directory_iterator(dir))
           if (e.is_regular_file() && now - e.last_write_time() < std::chrono::hours(24))
               result.push_back(e.path());
       return result;
   }
   ```

2. 参考代码：

   ```cpp
   void backup(const fs::path& file)
   {
       fs::path target = file;
       target += ".bak";                     // += 直接追加字符串，不加分隔符
       fs::copy_file(file, target, fs::copy_options::overwrite_existing);
   }
   ```

   注意这里要用 `+=` 而不是 `/`：`file / ".bak"` 会得到 `原名/.bak`。

3. 在遍历的同时删除文件，属于「遍历期间修改目录」，结果是未指定的。删除普通文件通常不会出问题，但不可靠。应该先把要删除的路径收集起来，遍历结束后再删除。另外，`fs::remove` 失败（比如没有权限）时会抛出异常，中断整个过程，可以改用 `error_code` 版本。

</details>
