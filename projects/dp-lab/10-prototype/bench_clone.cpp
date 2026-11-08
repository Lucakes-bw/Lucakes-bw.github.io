// 《原型》：构造代价高的对象——解析 .cube 文件建出 3D LUT，比较「每次重新解析」和「克隆一个原型」
// 编译：clang++ -std=c++17 -O2 bench_clone.cpp -o bench_clone（或 g++）；加参数 --sscanf 用每行都调用 sscanf 的慢写法解析
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct Filter {
    virtual ~Filter() = default;
    virtual std::unique_ptr<Filter> clone() const = 0;
};

// 3D LUT 调色滤镜：size × size × size 个格子，每个格子存一个 RGB 颜色
class Lut3D : public Filter {
public:
    explicit Lut3D(const std::string& cube, bool slowSscanf = false) {   // 构造：解析 .cube 文本
        const char* p = cube.c_str();
        char* end;
        while (*p) {
            if (slowSscanf ? std::sscanf(p, "LUT_3D_SIZE %d", &size_) == 1 : std::strncmp(p, "LUT_3D_SIZE", 11) == 0) {   // 先比较行首，不要每行都 sscanf（它会对剩余的整段文本求长度）
                if (!slowSscanf) size_ = std::atoi(p + 11);
                table_.reserve(size_ * size_ * size_ * 3);
            } else if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
                for (int k = 0; k < 3; ++k) { table_.push_back(std::strtof(p, &end)); p = end; }
            }
            while (*p && *p != '\n') ++p;                // 跳到下一行
            if (*p) ++p;
        }
    }
    std::unique_ptr<Filter> clone() const override { return std::make_unique<Lut3D>(*this); }   // 克隆：复制数组
    size_t bytes() const { return table_.size() * sizeof(float); }
    float at(size_t i) const { return table_[i]; }

private:
    int size_ = 0;
    std::vector<float> table_;
};

// 生成一个 33 × 33 × 33 的 .cube 文件内容（暖色调：红色略增强、蓝色略减弱）
std::string makeCube(int n) {
    std::string s = "TITLE \"warm\"\nLUT_3D_SIZE " + std::to_string(n) + "\n";
    char line[64];
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                float R = std::min(1.0f, r / float(n - 1) * 1.08f), G = g / float(n - 1), B = b / float(n - 1) * 0.92f;
                std::snprintf(line, sizeof line, "%.6f %.6f %.6f\n", R, G, B);
                s += line;
            }
    return s;
}

template <typename F>
double medianUs(F f, int runs = 31) {
    std::vector<double> t;
    for (int i = 0; i < runs; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
    }
    std::nth_element(t.begin(), t.begin() + runs / 2, t.end());
    return t[runs / 2];
}

volatile float sink;

int main(int argc, char** argv) {
    bool slow = argc > 1 && std::string(argv[1]) == "--sscanf";
    std::string cube = makeCube(33);
    Lut3D prototype(cube);
    std::printf(".cube 文本 %zu KB，LUT 表 %zu KB\n", cube.size() / 1024, prototype.bytes() / 1024);

    double parse = medianUs([&] { Lut3D f(cube, slow); sink = f.at(100); }, slow ? 5 : 31);
    double clone = medianUs([&] { auto f = prototype.clone(); sink = static_cast<Lut3D&>(*f).at(100); });
    std::printf("每次重新解析：%8.1f µs\n", parse);
    std::printf("克隆原型：    %8.1f µs（快 %.0f 倍）\n", clone, parse / clone);
}
