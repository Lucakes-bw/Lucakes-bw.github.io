// 《装饰器模式》：用 std::function 和 lambda 写装饰器——函数包函数，不需要定义类
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

struct Packet { int index = 0; bool key = false; int size = 0; };
using Reader = std::function<bool(Packet&)>;    // 「数据源」就是一个函数：读到返回 true

Reader memorySource() {
    return [i = 0](Packet& p) mutable {
        if (i >= 240) return false;
        p = {i, i % 48 == 0, i % 60 == 59 ? 0 : i % 48 == 0 ? 9000 : 1200 + i * 37 % 800};
        ++i;
        return true;
    };
}

Reader validate(Reader in) {                     // 每个装饰器是一个函数：拿一个 Reader，返回一个新的 Reader
    return [in = std::move(in)](Packet& p) {
        while (in(p)) if (p.size > 0) return true;
        return false;
    };
}

Reader limit(Reader in, int n) {                 // 只读前 n 个包
    return [in = std::move(in), n](Packet& p) mutable { return n-- > 0 && in(p); };
}

// 通用的函数装饰器：给任何可调用对象加上「调用计数」，参数和返回值原样转发
template <class F>
auto counted(std::string name, F f) {
    return [name = std::move(name), f = std::move(f), calls = 0](auto&&... args) mutable -> decltype(auto) {
        if (++calls % 100 == 0) std::printf("  [%s] 第 %d 次调用\n", name.c_str(), calls);
        return f(std::forward<decltype(args)>(args)...);
    };
}

int main() {
    Reader src = limit(validate(counted("source", memorySource())), 200);
    Packet p;
    int n = 0;
    while (src(p)) ++n;
    std::printf("读到 %d 个包，最后一个是 #%d\n", n, p.index);

    auto add = counted("add", [](int a, int b) { return a + b; });   // 普通函数也能装饰
    int sum = 0;
    for (int i = 0; i < 100; ++i) sum = add(sum, i);
    std::printf("sum = %d\n", sum);
}
