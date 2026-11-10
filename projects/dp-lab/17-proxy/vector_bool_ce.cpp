// 《代理模式》：编译不过——vector<bool> 的元素不是 bool，不能绑定到 bool&（故意的反例，不参与构建）
#include <vector>

template <class T>
void clear_all(std::vector<T>& v) {
    for (auto& x : v) x = T{};     // T = bool 时：*it 是一个临时的代理对象，不能绑定到 auto&
}

int main() {
    std::vector<int> a(4, 1);
    clear_all(a);                  // 没问题
    std::vector<bool> b(4, true);
    clear_all(b);                  // 编译错误
    bool& r = b[0];                // 编译错误：b[0] 不是 bool
    (void)r;
}
