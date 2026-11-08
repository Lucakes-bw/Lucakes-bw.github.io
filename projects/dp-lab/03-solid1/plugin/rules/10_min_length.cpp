#include "../rule.h"

class MinLength : public Rule {
public:
    std::string message() const override { return "至少 8 个字符"; }
    bool ok(const std::string& pwd) const override { return pwd.size() >= 8; }
};

extern "C" Rule* create_rule() { return new MinLength; }
