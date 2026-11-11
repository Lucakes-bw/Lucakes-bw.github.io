// 《策略模式》：播放器在运行时换策略——画面复杂、一帧来不及时从 bicubic 降到 bilinear，缓过来再升回去
#include <cstdio>
#include <memory>
#include <vector>

// 这里不真的缩放，每个策略返回「这一帧花了多少毫秒」，数值取自 quality.cpp 在 720p → 1080p 上的实测量级
class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual double scaleFrame() const = 0;
};
struct Nearest : Scaler { const char* name() const override { return "nearest"; }  double scaleFrame() const override { return 1; } };
struct Bilinear : Scaler { const char* name() const override { return "bilinear"; } double scaleFrame() const override { return 3; } };
struct Bicubic : Scaler { const char* name() const override { return "bicubic"; }  double scaleFrame() const override { return 21; } };
struct Lanczos : Scaler { const char* name() const override { return "lanczos"; }  double scaleFrame() const override { return 95; } };

class Player {
public:
    explicit Player(double budgetMs) : budget_(budgetMs) {
        ladder_.push_back(std::make_unique<Nearest>());      // 从便宜到贵排好
        ladder_.push_back(std::make_unique<Bilinear>());
        ladder_.push_back(std::make_unique<Bicubic>());
        ladder_.push_back(std::make_unique<Lanczos>());
    }
    void prefer(int level) { preferred_ = cur_ = level; }    // 用户在设置里选的画质

    void playFrame(int n, double decodeMs) {
        double total = decodeMs + ladder_[cur_]->scaleFrame();
        std::printf("帧 %2d  解码 %4.1f + %-8s → %5.1f ms", n, decodeMs, ladder_[cur_]->name(), total);
        if (total > budget_) {
            ++late_; calm_ = 0;
            std::printf("  超时");
            if (late_ >= 2 && cur_ > 0) { --cur_; late_ = 0; std::printf("  → 降级为 %s", ladder_[cur_]->name()); }
        } else {
            late_ = 0;
            bool upFits = cur_ < preferred_ && decodeMs + ladder_[cur_ + 1]->scaleFrame() < budget_ * 0.9;
            calm_ = upFits ? calm_ + 1 : 0;
            if (calm_ >= 3) { ++cur_; calm_ = 0; std::printf("  → 恢复为 %s", ladder_[cur_]->name()); }
        }
        std::printf("\n");
    }

private:
    double budget_;
    std::vector<std::unique_ptr<Scaler>> ladder_;
    int cur_ = 1, preferred_ = 1, late_ = 0, calm_ = 0;
};

int main() {
    Player player(1000.0 / 30);                             // 30 fps：每帧 33.3 ms
    player.prefer(2);                                       // 用户选了「高画质」：bicubic
    double decode[] = {6, 7, 6, 15, 16, 15, 14, 9, 6, 6, 7, 6, 6, 7};   // 第 4～7 帧是复杂的画面
    for (int i = 0; i < 14; ++i) player.playFrame(i + 1, decode[i]);
}
