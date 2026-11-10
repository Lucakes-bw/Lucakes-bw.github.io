// 《代理模式》：智能引用——shared_ptr 本身就是代理；operator-> 返回一个临时对象，就能在每次调用前后做事
#include <cstdio>
#include <memory>
#include <string>

struct Player {
    void play() { std::printf("    Player::play()\n"); }
    void seek(double sec) { std::printf("    Player::seek(%.1f)\n", sec); }
};

// 「调用前后各做一件事」的代理：operator-> 返回一个临时的 Guard
// Guard 构造时打印「进入」，它的 operator-> 再返回真实指针，表达式结束时 Guard 析构，打印「离开」
template <class T>
class Traced {
public:
    explicit Traced(std::shared_ptr<T> p) : p_(std::move(p)) {}
    class Guard {
    public:
        explicit Guard(T* p) : p_(p) { std::printf("  [进入] 加锁、计时、记日志……\n"); }
        ~Guard() { std::printf("  [离开] 解锁\n"); }
        T* operator->() const { return p_; }
    private:
        T* p_;
    };
    Guard operator->() const { return Guard(p_.get()); }

private:
    std::shared_ptr<T> p_;
};

int main() {
    // 1. shared_ptr：和 Player* 用法一样（-> 和 *），背后多做了引用计数，最后一个 shared_ptr 消失时自动 delete
    auto p = std::make_shared<Player>();
    {
        std::shared_ptr<Player> q = p;
        q->play();
        std::printf("use_count = %ld\n", p.use_count());
    }
    std::printf("use_count = %ld\n", p.use_count());

    // 2. operator-> 会一直「往下钻」，直到得到一个原始指针：Traced -> Guard -> Player*
    Traced<Player> t(p);
    t->seek(5);
    t->play();
}
