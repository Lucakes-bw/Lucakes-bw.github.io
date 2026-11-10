// 《代理模式》：练习 3——缓存代理和保护代理谁在外面？缓存套在外面，会员查过的付费片源，游客直接从缓存里拿到
#include <cstdio>
#include <map>
#include <string>

struct Session { bool vip = false; };            // 当前请求是谁发的（服务端每个请求更新一次）

struct UrlService {
    virtual ~UrlService() = default;
    virtual std::string url(const std::string& id) = 0;   // 返回播放地址；空字符串表示拒绝
};

class RealUrlService final : public UrlService {
public:
    std::string url(const std::string& id) override { ++calls; return "https://cdn.example/" + id + "?token=…"; }
    int calls = 0;
};

class Guard final : public UrlService {           // 保护代理：付费片（id 以 vip- 开头）只给会员
public:
    Guard(UrlService& inner, const Session& s) : inner_(inner), s_(s) {}
    std::string url(const std::string& id) override {
        if (id.rfind("vip-", 0) == 0 && !s_.vip) return "";
        return inner_.url(id);
    }
private:
    UrlService& inner_;
    const Session& s_;
};

class Cache final : public UrlService {           // 缓存代理：所有用户共用一份
public:
    explicit Cache(UrlService& inner) : inner_(inner) {}
    std::string url(const std::string& id) override {
        auto it = memo_.find(id);
        if (it != memo_.end()) return it->second;
        std::string u = inner_.url(id);
        if (!u.empty()) memo_[id] = u;
        return u;
    }
private:
    UrlService& inner_;
    std::map<std::string, std::string> memo_;
};

void run(const char* title, UrlService& front, Session& s, RealUrlService& real) {
    s.vip = true;
    std::string a = front.url("vip-movie");
    s.vip = false;
    std::string b = front.url("vip-movie");
    std::printf("%s\n  会员：%s\n  游客：%s\n  真实服务被调用 %d 次\n", title, a.empty() ? "拒绝" : a.c_str(),
                b.empty() ? "拒绝" : b.c_str(), real.calls);
}

int main() {
    {
        Session s; RealUrlService real;
        Guard guard(real, s);
        Cache cache(guard);                       // 错：Cache(Guard(Real))
        run("Cache 在外：", cache, s, real);
    }
    {
        Session s; RealUrlService real;
        Cache cache(real);
        Guard guard(cache, s);                    // 对：Guard(Cache(Real))，每次都先检查权限
        run("Guard 在外：", guard, s, real);
    }
}
