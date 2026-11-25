// 《CRTP 奇异递归模板》：对照版——经典虚函数基类逐采样点处理音频，每采样点一次虚调用且对象带 vptr
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

class VirtualFilter {
 public:
  virtual ~VirtualFilter() = default;
  virtual const char* name() const = 0;
  virtual float applySample(float x) const = 0;  // 热循环里每个采样点一次虚调用

  // 模板方法骨架：统计峰值并限幅到 [-1.0, 1.0]
  float processBuffer(std::vector<float>& pcm) const {
    float peak = 0.0f;
    for (float& s : pcm) {
      s = std::clamp(applySample(s), -1.0f, 1.0f);
      peak = std::max(peak, s < 0 ? -s : s);
    }
    return peak;
  }
};

class GainFilter final : public VirtualFilter {
 public:
  explicit GainFilter(float gain) : gain_(gain) {}
  const char* name() const override { return "Gain"; }
  float applySample(float x) const override { return x * gain_; }
 private:
  float gain_;
};

class SoftClipFilter final : public VirtualFilter {
 public:
  explicit SoftClipFilter(float drive) : drive_(drive) {}
  const char* name() const override { return "SoftClip"; }
  float applySample(float x) const override {
    float y = x * drive_;
    return y / (1.0f + (y < 0 ? -y : y));
  }
 private:
  float drive_;
};

int main() {
  std::vector<float> pcm = {0.2f, -0.5f, 0.8f, -0.9f};
  std::vector<std::unique_ptr<VirtualFilter>> chain;
  chain.push_back(std::make_unique<GainFilter>(1.5f));
  chain.push_back(std::make_unique<SoftClipFilter>(2.0f));

  for (const auto& f : chain) {
    float peak = f->processBuffer(pcm);
    std::printf("[%s] peak=%.3f first=%.3f\n", f->name(), peak, pcm[0]);
  }
  std::printf("sizeof(GainFilter)=%zu B (含 %zuB vptr + 4B float)\n",
              sizeof(GainFilter), sizeof(void*));
}
