// 《模板方法模式》：用组合代替继承——流程还是固定的，可变的步骤换成 std::function，可以在运行时拼装
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct Frame { int index; int w, h; };

using Step = std::function<bool(Frame&)>;        // 处理一帧；返回 false 丢掉这一帧
using Encode = std::function<int(const Frame&)>; // 编码一帧，返回字节数

class ExportPipeline {
public:
    ExportPipeline(std::string name, std::vector<Step> steps, Encode encode)
        : name_(std::move(name)), steps_(std::move(steps)), encode_(std::move(encode)) {}

    void run(int frames) const {                 // 流程和模板方法版一样，只写一遍
        int kept = 0;
        long bytes = 0;
        Frame out{};
        for (int i = 0; i < frames; ++i) {
            Frame f{i, 1280, 720};
            bool keep = true;
            for (const Step& s : steps_)
                if (!(keep = s(f))) break;
            if (!keep) continue;
            bytes += encode_(f);
            ++kept;
            out = f;
        }
        std::printf("%4d 帧 %4d×%-4d %9ld 字节  %s\n", kept, out.w, out.h, bytes, name_.c_str());
    }

private:
    std::string name_;
    std::vector<Step> steps_;
    Encode encode_;
};

Step everyNth(int n) { return [n](Frame& f) { return f.index % n == 0; }; }
Step scaleTo(int w, int h) { return [w, h](Frame& f) { f.w = w; f.h = h; return true; }; }
Encode h264() { return [](const Frame& f) { return f.w * f.h / 40; }; }
Encode gif() { return [](const Frame& f) { return f.w * f.h / 3; }; }

int main() {
    std::vector<ExportPipeline> jobs = {
        {"MP4",            {},                                 h264()},
        {"GIF",            {everyNth(2), scaleTo(480, 270)},   gif()},
        {"MP4 小尺寸",     {scaleTo(640, 360)},                h264()},   // 新组合：不用新写一个子类
        {"GIF 预览 6fps",  {everyNth(4), scaleTo(320, 180)},   gif()},
    };
    for (const auto& j : jobs) j.run(240);
}
