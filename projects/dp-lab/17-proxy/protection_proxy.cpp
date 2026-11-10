// 《代理模式》：保护代理——GuardedLibrary 在转发之前检查角色，没有权限就拒绝并记一条审计日志
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class Role { Guest, Member, Admin };
const char* name(Role r) { return r == Role::Guest ? "游客" : r == Role::Member ? "会员" : "管理员"; }

struct MediaLibrary {
    virtual ~MediaLibrary() = default;
    virtual int size() const = 0;
    virtual std::string play(int id) = 0;      // 返回播放结果；空字符串表示失败
    virtual bool erase(int id) = 0;
};

// 真实对象：只管业务，不知道有权限这回事
class LocalLibrary final : public MediaLibrary {
public:
    LocalLibrary() : titles_{"bbb-10s.mp4", "sintel.mkv", "tears.mp4"} {}
    int size() const override { return static_cast<int>(titles_.size()); }
    std::string play(int id) override {
        if (id < 0 || id >= size()) return "";
        return "正在播放 " + titles_[static_cast<std::size_t>(id)];
    }
    bool erase(int id) override {
        if (id < 0 || id >= size()) return false;
        titles_.erase(titles_.begin() + id);
        return true;
    }

private:
    std::vector<std::string> titles_;
};

// 保护代理：同样的接口，先检查再转发
class GuardedLibrary final : public MediaLibrary {
public:
    GuardedLibrary(MediaLibrary& real, Role role, std::vector<std::string>& audit)
        : real_(real), role_(role), audit_(audit) {}
    int size() const override { return real_.size(); }                   // 谁都能看
    std::string play(int id) override {
        if (role_ < Role::Member) { deny("play", id); return ""; }
        return real_.play(id);
    }
    bool erase(int id) override {
        if (role_ < Role::Admin) { deny("erase", id); return false; }
        return real_.erase(id);
    }

private:
    void deny(const char* op, int id) {
        audit_.push_back(std::string(name(role_)) + " 试图 " + op + "(" + std::to_string(id) + ")，已拒绝");
    }
    MediaLibrary& real_;
    Role role_;
    std::vector<std::string>& audit_;
};

// 调用方拿不到 LocalLibrary，只能通过 login 拿到一个代理
class Server {
public:
    std::unique_ptr<MediaLibrary> login(Role role) { return std::make_unique<GuardedLibrary>(lib_, role, audit_); }
    const std::vector<std::string>& audit() const { return audit_; }

private:
    LocalLibrary lib_;
    std::vector<std::string> audit_;
};

int main() {
    Server server;
    for (Role r : {Role::Guest, Role::Member, Role::Admin}) {
        auto lib = server.login(r);
        std::string played = lib->play(0);
        bool removed = lib->erase(2);
        std::printf("%s：size() → %d，play(0) → %s，erase(2) → %s\n", name(r), lib->size(),
                    played.empty() ? "拒绝" : played.c_str(), removed ? "成功" : "拒绝");
    }
    std::printf("审计日志：\n");
    for (auto& line : server.audit()) std::printf("  %s\n", line.c_str());
}
