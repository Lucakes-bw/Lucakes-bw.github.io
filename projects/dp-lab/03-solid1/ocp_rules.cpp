// 《SOLID 原则（上）》：规则变成对象，每个产品线自己挑规则；加规则不用改 validate
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct Rule {
    std::string message;                                  // 不通过时的提示
    std::function<bool(const std::string&)> ok;           // 检查是否通过
};

// 对修改关闭：以后不管加多少规则，这个函数都不用再改
std::vector<std::string> validate(const std::string& pwd, const std::vector<Rule>& rules) {
    std::vector<std::string> errors;
    for (const Rule& r : rules)
        if (!r.ok(pwd)) errors.push_back(r.message);
    return errors;
}

// 对扩展开放：每条规则是一个返回 Rule 的函数，新规则就是新函数
Rule minLength(size_t n) {
    return {"至少 " + std::to_string(n) + " 个字符", [n](const std::string& s) { return s.size() >= n; }};
}
Rule hasDigit() {
    return {"至少包含一个数字", [](const std::string& s) {
                return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
            }};
}
Rule hasUpper() {
    return {"至少包含一个大写字母", [](const std::string& s) {
                return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isupper(c); });
            }};
}
Rule notCommon() {
    return {"不能使用常见密码", [](const std::string& s) {
                static const std::vector<std::string> common = {"password", "Passw0rd", "12345678", "qwerty123"};
                return std::find(common.begin(), common.end(), s) == common.end();
            }};
}

int main() {
    const std::vector<Rule> website = {minLength(8), hasDigit(), hasUpper(), notCommon()};
    const std::vector<Rule> admin = {minLength(12), hasDigit(), hasUpper(), notCommon()};
    const std::vector<Rule> tool = {minLength(6), hasDigit()};

    for (const auto& e : validate("Passw0rd", website)) std::cout << "官网：" << e << '\n';
    for (const auto& e : validate("Passw0rd", admin)) std::cout << "后台：" << e << '\n';
    for (const auto& e : validate("abc", tool)) std::cout << "内部工具：" << e << '\n';
}
