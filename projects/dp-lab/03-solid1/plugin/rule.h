// 《SOLID 原则（上）》插件实验：主程序和插件之间唯一的约定
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
