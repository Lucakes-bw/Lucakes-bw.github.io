// 《访问者模式》练习 3：用 std::variant 表示一条滤镜链，两种操作：拼出 ffmpeg 的 -vf 参数、算出每一步之后的画面尺寸和每秒要处理的像素
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

struct Scale { int w, h; };
struct Crop { int w, h, x, y; };
struct Fps { int fps; };
struct Hflip {};
using Filter = std::variant<Scale, Crop, Fps, Hflip>;

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

// 操作 1：拼成 ffmpeg 的滤镜描述，返回字符串
std::string describe(const std::vector<Filter>& chain) {
  std::string s;
  for (auto& f : chain) {
    if (!s.empty()) s += ',';
    s += std::visit(overloaded{
        [](const Scale& x) { return "scale=" + std::to_string(x.w) + ":" + std::to_string(x.h); },
        [](const Crop& x) { return "crop=" + std::to_string(x.w) + ":" + std::to_string(x.h) + ":" +
                                   std::to_string(x.x) + ":" + std::to_string(x.y); },
        [](const Fps& x) { return "fps=" + std::to_string(x.fps); },
        [](const Hflip&) { return std::string("hflip"); },
    }, f);
  }
  return s;
}

// 操作 2：沿着链往下推算画面格式；带状态，所以写成一个函数对象
struct Shape { int w, h, fps; };
struct Estimator {
  Shape cur;
  double pixels = 0;                      // 每秒一共要处理（读入）多少像素
  void operator()(const Scale& x) { pixels += 1.0 * cur.w * cur.h * cur.fps; cur.w = x.w; cur.h = x.h; }
  void operator()(const Crop& x) { pixels += 1.0 * cur.w * cur.h * cur.fps; cur.w = x.w; cur.h = x.h; }
  void operator()(const Fps& x) { cur.fps = x.fps; }      // 丢帧几乎不花时间
  void operator()(const Hflip&) { pixels += 1.0 * cur.w * cur.h * cur.fps; }
};

int main() {
  std::vector<Filter> a = {Scale{640, 360}, Hflip{}, Fps{12}};
  std::vector<Filter> b = {Fps{12}, Scale{640, 360}, Hflip{}};   // 同样的效果，先丢帧
  for (auto* chain : {&a, &b}) {
    Estimator e{{1280, 720, 24}};         // bbb-10s.mp4：1280×720，24 fps
    for (auto& f : *chain) std::visit(e, f);
    std::printf("-vf %-32s → %d×%d@%d，每秒处理 %.1f 百万像素\n", describe(*chain).c_str(), e.cur.w, e.cur.h,
                e.cur.fps, e.pixels / 1e6);
  }
}
