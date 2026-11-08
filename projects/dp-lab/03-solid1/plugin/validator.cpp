// 《SOLID 原则（上）》插件实验的主程序：启动时加载 plugins/ 下的每一个动态库，每个动态库提供一条规则
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
