// 《迭代器模式》：故意编译不过（C++20）——std::ranges::sort 用 concept 检查迭代器分类，报错直接说「不是 random_access_range」
#include <algorithm>
#include <forward_list>

int main() {
    std::forward_list<long long> pts = {3072, 0, 2048, 1024};
    std::ranges::sort(pts);
}
