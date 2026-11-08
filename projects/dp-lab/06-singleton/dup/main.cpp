// 《单例》：两个动态库各自调用 Registry::instance()，拿到的是同一个对象吗？
#include <cstdio>
#include "registry.h"

extern "C" void* registry_from_a();
extern "C" void* registry_from_b();

int main() {
    void* a = registry_from_a();
    void* b = registry_from_b();
    std::printf("库 A 拿到 %p，库 B 拿到 %p，%s\n", a, b, a == b ? "是同一个对象" : "是两个不同的对象！");
    std::printf("库 A 看到的 count = %d，库 B 看到的 count = %d\n",
                static_cast<Registry*>(a)->count, static_cast<Registry*>(b)->count);
}
