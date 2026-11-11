// 《命令模式》练习 4（修好）：执行时记下旧值，撤销时直接恢复旧值，不做反向运算
#include <algorithm>
#include <cstdio>

struct Clip { double volume = 1.0; };

class AdjustVolume {
public:
    AdjustVolume(Clip& c, double delta) : c_(c), delta_(delta) {}
    void execute() {
        old_ = c_.volume;
        c_.volume = std::clamp(c_.volume + delta_, 0.0, 2.0);
    }
    void undo() { c_.volume = old_; }

private:
    Clip& c_;
    double delta_, old_ = 0;
};

int main() {
    Clip clip;
    clip.volume = 1.5;
    AdjustVolume up(clip, 1.0);
    up.execute();
    std::printf("1.5 加 1.0：%.1f（限制在 2.0）\n", clip.volume);
    up.undo();
    std::printf("撤销：%.1f\n", clip.volume);
    for (int i = 0; i < 10; ++i) { up.execute(); up.undo(); }
    std::printf("再执行、撤销 10 个来回：%.17g\n", clip.volume);
}
