// 《为什么需要设计模式》练习 2：用 enum class + 不带 default 的 switch，让编译器发现漏改的分支
#include <iostream>

enum class Format { Csv, Json, Html };

void printFooter(Format fmt) {
    switch (fmt) {           // 故意漏掉 Format::Html，也不写 default
    case Format::Csv:
        break;
    case Format::Json:
        std::cout << "]\n";
        break;
    }
}

int main() {
    printFooter(Format::Json);
    printFooter(Format::Html);   // 什么也不输出
}
