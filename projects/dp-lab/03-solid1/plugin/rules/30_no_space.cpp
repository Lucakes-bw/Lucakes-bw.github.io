// 后来新加的规则：不能包含空格。主程序 validator 不用重新编译
#include "../rule.h"

class NoSpace : public Rule {
public:
    std::string message() const override { return "不能包含空格"; }
    bool ok(const std::string& pwd) const override { return pwd.find(' ') == std::string::npos; }
};

extern "C" Rule* create_rule() { return new NoSpace; }
