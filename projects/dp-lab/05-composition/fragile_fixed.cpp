// 《组合优于继承》：改成组合，CountingSet 只通过公开接口使用 IntSet
#include <iostream>
#include <set>
#include <vector>

class IntSet {
public:
    bool insert(int x) { return s_.insert(x).second; }
    void insertAll(const std::vector<int>& xs) {
        for (int x : xs) insert(x);
    }
    size_t size() const { return s_.size(); }

private:
    std::set<int> s_;
};

class CountingSet {                          // 不是一种 IntSet，而是「持有」一个 IntSet
public:
    bool insert(int x) {
        ++attempts_;
        return set_.insert(x);
    }
    void insertAll(const std::vector<int>& xs) {
        attempts_ += xs.size();
        set_.insertAll(xs);                  // IntSet 内部怎么实现，都调不回 CountingSet
    }
    size_t size() const { return set_.size(); }
    size_t attempts() const { return attempts_; }

private:
    IntSet set_;
    size_t attempts_ = 0;
};

int main() {
    CountingSet s;
    s.insertAll({1, 2, 3});
    s.insert(2);
    std::cout << "元素个数 " << s.size() << "，尝试插入次数 " << s.attempts() << "（期望 4）\n";
}
