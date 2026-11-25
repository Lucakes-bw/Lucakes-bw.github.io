#include <cctype>
#include <memory>
#include <string>
#include <vector>
using namespace std;

struct LogContext {
    int level;
    string module;
    string msg;
};

class LogFilter {
public:
    virtual ~LogFilter() = default;
    virtual bool apply(LogContext& ctx) const = 0;
};

class LevelFilter final : public LogFilter {
public:
    explicit LevelFilter(int minLvl) : min_(minLvl) {}
    bool apply(LogContext& ctx) const override {
        return ctx.level >= min_;
    }
private:
    int min_;
};

class DenyModuleFilter final : public LogFilter {
public:
    explicit DenyModuleFilter(string blocked) : blocked_(std::move(blocked)) {}
    bool apply(LogContext& ctx) const override {
        return ctx.module != blocked_;
    }
private:
    string blocked_;
};

class RedactTokenFilter final : public LogFilter {
public:
    bool apply(LogContext& ctx) const override {
        size_t pos = ctx.msg.find("token=");
        if (pos != string::npos) {
            size_t end = pos + 6;
            while (end < ctx.msg.size() &&
                   (isalnum(static_cast<unsigned char>(ctx.msg[end])) || ctx.msg[end] == '_')) {
                ++end;
            }
            if (end > pos + 6) {
                ctx.msg.replace(pos + 6, end - (pos + 6), "***");
            }
        }
        return true;
    }
};

class TruncateFilter final : public LogFilter {
public:
    explicit TruncateFilter(size_t maxLen) : maxLen_(maxLen) {}
    bool apply(LogContext& ctx) const override {
        if (ctx.msg.size() > maxLen_) {
            ctx.msg = ctx.msg.substr(0, maxLen_) + "...";
        }
        return true;
    }
private:
    size_t maxLen_;
};

class Solution {
public:
    string processLog(int level, string module, string msg, int minLevel, string denyModule) {
        vector<unique_ptr<LogFilter>> pipeline;
        pipeline.push_back(make_unique<LevelFilter>(minLevel));
        pipeline.push_back(make_unique<DenyModuleFilter>(std::move(denyModule)));
        pipeline.push_back(make_unique<RedactTokenFilter>());
        pipeline.push_back(make_unique<TruncateFilter>(32));

        LogContext ctx{level, std::move(module), std::move(msg)};
        for (const auto& f : pipeline) {
            if (!f->apply(ctx)) return "";
        }
        return ctx.msg;
    }
};
