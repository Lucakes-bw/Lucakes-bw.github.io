// 《模板方法模式》：练习 3——测试夹具：run() 固定是 setUp → 测试体 → tearDown，测试失败了 tearDown 也一定执行
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int g_open = 0;                                  // 打开着的播放器个数，用来检查 tearDown 有没有漏

struct Player {                                  // 被测的对象（模拟）
    double pos = 0;
    bool open(const std::string& path) { ++g_open; return path.size() > 4; }
    void close() { --g_open; }
    void seek(double t) { pos = t < 10 ? t : 10; }
};

class TestCase {
public:
    explicit TestCase(std::string name) : name_(std::move(name)) {}
    virtual ~TestCase() = default;

    bool run() {                                 // 模板方法：流程固定，子类改不了
        failures_ = 0;
        if (setUp()) body();
        else check(false, "setUp 失败，跳过测试体");
        tearDown();                              // 不管 setUp、测试体成没成功都执行（和 GoogleTest 一样）
        std::printf("[%s] %s\n", failures_ == 0 ? "  OK  " : " FAIL ", name_.c_str());
        return failures_ == 0;
    }

protected:
    virtual bool setUp() { return true; }        // 钩子
    virtual void tearDown() {}                   // 钩子
    virtual void body() = 0;                     // 基本操作：每个测试必须写

    void check(bool ok, const char* what) {      // 不用异常：记下失败，接着往下跑
        if (!ok) { ++failures_; std::printf("         检查失败：%s\n", what); }
    }

private:
    std::string name_;
    int failures_ = 0;
};

class PlayerTest : public TestCase {             // 夹具：每个测试前打开文件，测试后关掉
public:
    using TestCase::TestCase;

protected:
    Player player;
    bool setUp() override { return player.open("bbb-10s.mp4"); }
    void tearDown() override { player.close(); }
};

class SeekInside : public PlayerTest {
public:
    SeekInside() : PlayerTest("seek 到 5 秒") {}
protected:
    void body() override { player.seek(5); check(player.pos == 5, "pos == 5"); }
};

class SeekPastEnd : public PlayerTest {
public:
    SeekPastEnd() : PlayerTest("seek 超过结尾") {}
protected:
    void body() override { player.seek(99); check(player.pos == 99, "pos == 99"); }   // 故意写错的期望值
};

class OpenBadFile : public PlayerTest {
public:
    OpenBadFile() : PlayerTest("打开不存在的文件") {}
protected:
    bool setUp() override { return player.open("x"); }   // 重写夹具：打开失败
    void body() override { check(player.pos == 0, "pos == 0"); }
};

int main() {
    std::vector<std::unique_ptr<TestCase>> tests;
    tests.push_back(std::make_unique<SeekInside>());
    tests.push_back(std::make_unique<SeekPastEnd>());
    tests.push_back(std::make_unique<OpenBadFile>());
    int passed = 0;
    for (auto& t : tests) passed += t->run();
    std::printf("通过 %d / %zu，打开着的播放器：%d\n", passed, tests.size(), g_open);
}
