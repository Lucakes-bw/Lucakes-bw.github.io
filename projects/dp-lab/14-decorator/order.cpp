// 《装饰器模式》：包装的顺序会改变结果——先放大再限幅，和先限幅再放大
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

class AudioSource {                              // 一个一个地读出采样（浮点，正常范围是 -1～1）
public:
    virtual ~AudioSource() = default;
    virtual bool read(float& s) = 0;
    virtual std::string name() const = 0;
};

class Sine : public AudioSource {                // 1 kHz 正弦波，48 kHz 采样，10 毫秒，幅度 0.4
public:
    bool read(float& s) override {
        if (i_ == 480) return false;
        s = 0.4f * static_cast<float>(std::sin(2 * 3.141592653589793 * 1000 * i_++ / 48000));
        return true;
    }
    std::string name() const override { return "Sine"; }
private:
    int i_ = 0;
};

class Filter : public AudioSource {              // 装饰器基类：持有里面那一层
public:
    explicit Filter(std::unique_ptr<AudioSource> in) : in_(std::move(in)) {}
protected:
    std::unique_ptr<AudioSource> in_;
};

class Gain : public Filter {                     // 音量 × k
public:
    Gain(std::unique_ptr<AudioSource> in, float k) : Filter(std::move(in)), k_(k) {}
    bool read(float& s) override {
        if (!in_->read(s)) return false;
        s *= k_;
        return true;
    }
    std::string name() const override { return "Gain(" + in_->name() + ")"; }
private:
    float k_;
};

class Clip : public Filter {                     // 限幅：超出 ±1 的部分削掉
public:
    using Filter::Filter;
    bool read(float& s) override {
        if (!in_->read(s)) return false;
        s = std::clamp(s, -1.0f, 1.0f);
        return true;
    }
    std::string name() const override { return "Clip(" + in_->name() + ")"; }
};

void report(AudioSource& src) {
    float s, peak = 0;
    int n = 0, over = 0;
    while (src.read(s)) {
        peak = std::max(peak, std::fabs(s));
        if (std::fabs(s) > 1.0f) ++over;         // 超出范围：转成 16 位整数时会溢出，听起来是爆音
        ++n;
    }
    std::printf("%-16s %d 个采样，峰值 %.3f，超出范围 %3d 个\n", src.name().c_str(), n, peak, over);
}

int main() {
    report(*std::make_unique<Sine>());
    report(*std::make_unique<Clip>(std::make_unique<Gain>(std::make_unique<Sine>(), 4.0f)));
    report(*std::make_unique<Gain>(std::make_unique<Clip>(std::make_unique<Sine>()), 4.0f));
}
