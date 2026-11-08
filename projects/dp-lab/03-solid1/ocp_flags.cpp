// 《SOLID 原则（上）》：密码校验，每个产品线要的规则不一样，于是参数越加越多
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
