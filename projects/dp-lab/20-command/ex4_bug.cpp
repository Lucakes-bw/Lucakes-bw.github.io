// 《命令模式》练习 4（有 bug）：「音量 +x」命令执行时把音量限制在 0～2，撤销时减回 x，撤销后音量不对
#include <algorithm>
#include <cstdio>

struct Clip { double volume = 1.0; };

class AdjustVolume {
public:
    AdjustVolume(Clip& c, double delta) : c_(c), delta_(delta) {}
    void execute() { c_.volume = std::clamp(c_.volume + delta_, 0.0, 2.0); }
    void undo() { c_.volume -= delta_; }

private:
    Clip& c_;
    double delta_;
};

int main() {
    Clip clip;
    clip.volume = 1.5;
    AdjustVolume up(clip, 1.0);
    up.execute();
    std::printf("1.5 加 1.0：%.1f（限制在 2.0）\n", clip.volume);
    up.undo();
    std::printf("撤销：%.1f（应该是 1.5）\n", clip.volume);
}
