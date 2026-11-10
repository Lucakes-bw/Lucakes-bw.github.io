// 《适配器》：标准库里到处是适配器
#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <stack>
#include <vector>

// 一个旧函数：参数顺序和单位都和我们需要的不一样
double legacyVolume(int channel, double percent) { return percent / 100.0 + channel * 0; }

int main() {
    // 容器适配器：std::stack 把 vector 的接口（push_back / back / pop_back）适配成栈的接口（push / top / pop）
    std::stack<int, std::vector<int>> undo;
    for (int i = 1; i <= 3; ++i) undo.push(i);
    std::cout << "栈顶 " << undo.top() << '\n';

    // 迭代器适配器：back_inserter 把「赋值给迭代器」适配成「push_back」
    std::vector<int> src = {3, 1, 2}, dst;
    std::copy(src.begin(), src.end(), std::back_inserter(dst));
    // reverse_iterator 把「往前走」适配成「往后走」
    std::cout << "倒着输出 ";
    std::copy(dst.rbegin(), dst.rend(), std::ostream_iterator<int>(std::cout, " "));
    std::cout << '\n';

    // 函数适配器：新代码需要 function<double(double)>（输入 0～1 的音量），旧函数是 (声道, 百分比)
    std::function<double(double)> setVolume = [](double v) { return legacyVolume(0, v * 100); };
    std::cout << "音量 " << setVolume(0.5) << '\n';
}
