// 《策略模式》：练习 3——编码器的码率控制做成策略：CQP、CBR、CRF 决定每一帧用多大的量化参数 QP
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

// 简化的码率模型：画面越复杂、QP 越小，这一帧的比特数越多；QP 每加 6，比特数大约减半
int frameBits(double complexity, int qp) { return static_cast<int>(complexity * 40000 * std::pow(2.0, (26 - qp) / 6.0)); }

class RateControl {                              // 策略接口
public:
    virtual ~RateControl() = default;
    virtual const char* name() const = 0;
    virtual int qpFor(double complexity) = 0;    // 编码前：这一帧用多大的 QP
    virtual void onEncoded(int bits) { (void)bits; }   // 编码后：告诉策略实际花了多少比特
};

// 固定 QP：最简单，码率随画面起伏
class Cqp : public RateControl {
public:
    explicit Cqp(int qp) : qp_(qp) {}
    const char* name() const override { return "CQP"; }
    int qpFor(double) override { return qp_; }
private:
    int qp_;
};

// 恒定码率：按复杂度估算让这一帧正好花掉预算的 QP；之前花超的记在缓冲区里，分几帧还回去（有状态）
class Cbr : public RateControl {
public:
    explicit Cbr(int bitsPerFrame) : target_(bitsPerFrame) {}
    const char* name() const override { return "CBR"; }
    int qpFor(double complexity) override {
        double want = std::max(target_ - buffer_ / 4.0, target_ * 0.25);
        return std::clamp(26 + static_cast<int>(std::lround(6 * std::log2(complexity * 40000 / want))), 10, 51);
    }
    void onEncoded(int bits) override { buffer_ += bits - target_; }
private:
    int target_, buffer_ = 0;
};

// 恒定质量：复杂的画面人眼不容易看出损失，可以多量化一点
class Crf : public RateControl {
public:
    explicit Crf(int crf) : crf_(crf) {}
    const char* name() const override { return "CRF"; }
    int qpFor(double complexity) override { return crf_ + static_cast<int>(std::lround(3 * std::log2(complexity))); }
private:
    int crf_;
};

class Encoder {                                  // 上下文
public:
    explicit Encoder(std::unique_ptr<RateControl> rc) : rc_(std::move(rc)) {}
    void encode(const std::vector<double>& complexity) {
        long total = 0;
        int lo = 1 << 30, hi = 0;
        std::printf("%-4s QP:", rc_->name());
        for (double c : complexity) {
            int qp = rc_->qpFor(c);
            int bits = frameBits(c, qp);
            rc_->onEncoded(bits);
            total += bits;
            lo = std::min(lo, bits), hi = std::max(hi, bits);
            std::printf(" %2d", qp);
        }
        std::printf("   每帧 %3d～%3d kbit，共 %ld kbit\n", lo / 1000, hi / 1000, total / 1000);
    }
private:
    std::unique_ptr<RateControl> rc_;
};

int main() {
    std::vector<double> scene = {1, 1, 1, 1, 4, 4, 4, 4, 1, 1, 1, 1};   // 中间 4 帧是复杂画面
    Encoder(std::make_unique<Cqp>(26)).encode(scene);
    Encoder(std::make_unique<Cbr>(80000)).encode(scene);
    Encoder(std::make_unique<Crf>(26)).encode(scene);
}
