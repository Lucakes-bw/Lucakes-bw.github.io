// 《SOLID 原则（下）》练习：std::vector<bool> 不是一个真正的 vector<T>（故意编译不过）
#include <vector>

template <typename T>
void resetFirst(std::vector<T>& v) {
    T& first = v[0];        // 对任何 vector<T> 都成立？
    first = T();
}

int main() {
    std::vector<int> a = {1, 2, 3};
    resetFirst(a);          // 没问题
    std::vector<bool> b = {true, false};
    resetFirst(b);          // 编译错误
}
