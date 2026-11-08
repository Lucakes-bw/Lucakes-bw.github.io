// 《组合优于继承》：子类依赖了基类的实现细节，统计结果翻倍
#include <iostream>
#include <set>
#include <vector>

class IntSet {
public:
    virtual ~IntSet() = default;
    virtual bool insert(int x) { return s_.insert(x).second; }
    virtual void insertAll(const std::vector<int>& xs) {
        for (int x : xs) insert(x);          // 实现细节：逐个调用 insert
    }
    size_t size() const { return s_.size(); }

private:
    std::set<int> s_;
};

// 想统计「一共尝试插入过多少个元素」，于是继承 IntSet，重写两个插入函数
class CountingSet : public IntSet {
public:
    bool insert(int x) override {
        ++attempts_;
        return IntSet::insert(x);
    }
    void insertAll(const std::vector<int>& xs) override {
        attempts_ += xs.size();
        IntSet::insertAll(xs);               // 基类里又会调用 insert，而 insert 是虚函数……
    }
    size_t attempts() const { return attempts_; }

private:
    size_t attempts_ = 0;
};

int main() {
    CountingSet s;
    s.insertAll({1, 2, 3});
    std::cout << "元素个数 " << s.size() << "，尝试插入次数 " << s.attempts() << "（期望 3）\n";
}
