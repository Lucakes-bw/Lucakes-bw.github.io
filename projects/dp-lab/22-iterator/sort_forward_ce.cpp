// 《迭代器模式》：故意编译不过——对只有 forward 迭代器的容器调用 std::sort（std::forward_list 也一样）
#include <algorithm>
#include <forward_list>

int main() {
    std::forward_list<long long> pts = {3072, 0, 2048, 1024};
    std::sort(pts.begin(), pts.end());   // sort 要随机访问：it + n、it2 - it1
    pts.sort();                          // 正确做法：容器自己提供的成员函数（归并排序，只改指针）
}
