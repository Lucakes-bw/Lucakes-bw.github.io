#include <algorithm>
#include <cctype>
#include "../rule.h"

class HasDigit : public Rule {
public:
    std::string message() const override { return "至少包含一个数字"; }
    bool ok(const std::string& pwd) const override {
        return std::any_of(pwd.begin(), pwd.end(), [](unsigned char c) { return std::isdigit(c); });
    }
};

extern "C" Rule* create_rule() { return new HasDigit; }
