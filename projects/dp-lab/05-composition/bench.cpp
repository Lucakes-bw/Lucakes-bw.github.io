// 《组合优于继承》：四种多态写法求 100 万个形状的总面积，比较每个元素的耗时
// 编译：clang++ -std=c++17 -O2 bench.cpp -o bench（或 g++）
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <variant>
#include <vector>

constexpr int N = 1'000'000;
constexpr int PASSES = 31;

// ---------- 基准：enum + switch ----------
enum class Kind : uint8_t { Circle, Rect, Tri };
struct Tagged { Kind k; double a, b; };

double areaOf(const Tagged& t) {
    switch (t.k) {
    case Kind::Circle: return 3.14159 * t.a * t.a;
    case Kind::Rect: return t.a * t.b;
    case Kind::Tri: return 0.5 * t.a * t.b;
    }
    return 0;
}

// ---------- ① 虚函数 ----------
struct Shape { virtual ~Shape() = default; virtual double area() const = 0; };
struct VCircle final : Shape { double r; explicit VCircle(double r) : r(r) {} double area() const override { return 3.14159 * r * r; } };
struct VRect final : Shape { double w, h; VRect(double w, double h) : w(w), h(h) {} double area() const override { return w * h; } };
struct VTri final : Shape { double b, h; VTri(double b, double h) : b(b), h(h) {} double area() const override { return 0.5 * b * h; } };

// ---------- ②③④ 用的普通类 ----------
struct Circle { double r; double area() const { return 3.14159 * r * r; } };
struct Rect { double w, h; double area() const { return w * h; } };
struct Tri { double b, h; double area() const { return 0.5 * b * h; } };
using AnyShape = std::variant<Circle, Rect, Tri>;

template <typename T>
double totalOf(const std::vector<T>& v) {
    double s = 0;
    for (const auto& x : v) s += x.area();
    return s;
}

volatile double sink;

// 运行 PASSES 次，返回每个元素耗时的中位数（纳秒）
template <typename F>
double measure(F&& body) {
    std::vector<double> ns;
    for (int p = 0; p < PASSES; ++p) {
        auto t0 = std::chrono::steady_clock::now();
        sink = body();
        ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N);
    }
    std::nth_element(ns.begin(), ns.begin() + PASSES / 2, ns.end());
    return ns[PASSES / 2];
}

struct Result { double sw, virt, func, var, tmpl; };

Result run(const std::vector<Tagged>& data, bool scatterHeap) {
    std::vector<std::unique_ptr<Shape>> virt;
    std::vector<std::function<double()>> func;
    std::vector<AnyShape> var;
    std::vector<Circle> cs; std::vector<Rect> rs; std::vector<Tri> ts;
    for (const auto& t : data) {
        switch (t.k) {
        case Kind::Circle: virt.push_back(std::make_unique<VCircle>(t.a)); func.push_back([c = Circle{t.a}] { return c.area(); });
                           var.push_back(Circle{t.a}); cs.push_back({t.a}); break;
        case Kind::Rect: virt.push_back(std::make_unique<VRect>(t.a, t.b)); func.push_back([r = Rect{t.a, t.b}] { return r.area(); });
                         var.push_back(Rect{t.a, t.b}); rs.push_back({t.a, t.b}); break;
        case Kind::Tri: virt.push_back(std::make_unique<VTri>(t.a, t.b)); func.push_back([x = Tri{t.a, t.b}] { return x.area(); });
                        var.push_back(Tri{t.a, t.b}); ts.push_back({t.a, t.b}); break;
        }
    }
    if (scatterHeap) std::shuffle(virt.begin(), virt.end(), std::mt19937(7));   // 对象在内存中的顺序和遍历顺序无关

    Result r;
    r.sw = measure([&] { double s = 0; for (const auto& t : data) s += areaOf(t); return s; });
    r.virt = measure([&] { double s = 0; for (const auto& p : virt) s += p->area(); return s; });
    r.func = measure([&] { double s = 0; for (const auto& f : func) s += f(); return s; });
    r.var = measure([&] { double s = 0; for (const auto& v : var) s += std::visit([](const auto& x) { return x.area(); }, v); return s; });
    r.tmpl = measure([&] { return totalOf(cs) + totalOf(rs) + totalOf(ts); });
    return r;
}

int main() {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> kind(0, 2);
    std::uniform_real_distribution<double> len(1.0, 2.0);
    std::vector<Tagged> data(N);
    for (auto& t : data) t = {static_cast<Kind>(kind(rng)), len(rng), len(rng)};

    std::printf("sizeof：Tagged %zu，unique_ptr %zu（另有堆上的对象 %zu～%zu），std::function %zu，variant %zu\n",
                sizeof(Tagged), sizeof(std::unique_ptr<Shape>), sizeof(VCircle), sizeof(VRect), sizeof(std::function<double()>), sizeof(AnyShape));

    Result a = run(data, false);
    Result c = run(data, true);
    std::vector<Tagged> sorted = data;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Tagged& x, const Tagged& y) { return x.k < y.k; });
    Result b = run(sorted, false);

    std::printf("%-16s %10s %10s %14s\n", "ns / 元素", "随机顺序", "按类型排序", "随机+对象打乱");
    std::printf("%-16s %10.2f %10.2f %14s\n", "enum + switch", a.sw, b.sw, "-");
    std::printf("%-16s %10.2f %10.2f %14.2f\n", "① 虚函数", a.virt, b.virt, c.virt);
    std::printf("%-16s %10.2f %10.2f %14s\n", "② std::function", a.func, b.func, "-");
    std::printf("%-16s %10.2f %10.2f %14s\n", "④ std::variant", a.var, b.var, "-");
    std::printf("%-16s %10.2f %10.2f %14s\n", "③ 模板（分桶）", a.tmpl, b.tmpl, "-");
}
