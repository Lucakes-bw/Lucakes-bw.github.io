// 《CRTP 奇异递归模板》：压测——处理 1000 万音频采样点（每帧 4000 采样点 × 2500 帧，模拟真实 DSP 块处理），
// 对比「虚函数逐采样点调用」vs「CRTP 静态多态（内联 + SIMD 向量化）」
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr size_t kBlockSize = 4000;      // 单帧 4000 采样点 = 16 KB（驻留 L1 缓存，符合真实 DSP 块大小）
constexpr int kBlocks = 2500;            // 2500 帧 = 1000 万采样点
constexpr size_t kTotalSamples = kBlockSize * kBlocks;
constexpr int kRounds = 7;

// 方案 A：经典虚函数基类（每采样点一次虚调用，无法内联，也无法被自动向量化）
class VirtualFilter {
 public:
  virtual ~VirtualFilter() = default;
  virtual float applySample(float x) const = 0;

  __attribute__((noinline))
  void processBlock(float* pcm, size_t n) const {
    for (size_t i = 0; i < n; ++i) {
      float y = applySample(pcm[i]);
      pcm[i] = y > 1.0f ? 1.0f : (y < -1.0f ? -1.0f : y);
    }
  }
};

class VirtualGain final : public VirtualFilter {
 public:
  explicit VirtualGain(float g) : gain_(g) {}
  float applySample(float x) const override { return x * gain_; }
 private:
  float gain_;
};

class VirtualSoftClip final : public VirtualFilter {
 public:
  explicit VirtualSoftClip(float d) : drive_(d) {}
  float applySample(float x) const override {
    float y = x * drive_;
    return y / (1.0f + (y < 0 ? -y : y));
  }
 private:
  float drive_;
};

// 阻止编译器在编译期推断出具体子类（模拟运行期按配置/插件动态创建）
volatile int g_filterKind = 0;

__attribute__((noinline))
std::unique_ptr<VirtualFilter> makeVirtualFilter(float param) {
  if (g_filterKind == 0) return std::make_unique<VirtualGain>(param);
  return std::make_unique<VirtualSoftClip>(param);
}

// 方案 B：CRTP 静态多态（编译期展开 applySample，编译器可完全内联并触发 SIMD 向量化）
template <class Derived>
class CrtpFilter {
 public:
  __attribute__((noinline))
  void processBlock(float* pcm, size_t n) const {
    const auto& self = static_cast<const Derived&>(*this);
    for (size_t i = 0; i < n; ++i) {
      float y = self.applySample(pcm[i]);
      pcm[i] = y > 1.0f ? 1.0f : (y < -1.0f ? -1.0f : y);
    }
  }
};

class CrtpGain : public CrtpFilter<CrtpGain> {
 public:
  explicit CrtpGain(float g) : gain_(g) {}
  float applySample(float x) const { return x * gain_; }
 private:
  float gain_;
};

volatile float g_sink = 0.0f;

double median(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

int main() {
  std::vector<float> src(kBlockSize);
  for (size_t i = 0; i < kBlockSize; ++i) {
    src[i] = static_cast<float>((i % 2000) - 1000) / 1500.0f;
  }

  auto vFilter = makeVirtualFilter(1.25f);
  CrtpGain cFilter(1.25f);

  std::vector<double> virtMs, crtpMs;
  std::vector<float> buf(kBlockSize);

  for (int r = 0; r < kRounds; ++r) {
    // 1. 虚函数逐采样点调用
    buf = src;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < kBlocks; ++b) {
      vFilter->processBlock(buf.data(), buf.size());
    }
    auto t1 = std::chrono::steady_clock::now();
    g_sink += buf[0];
    virtMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

    // 2. CRTP 静态多态
    buf = src;
    auto t2 = std::chrono::steady_clock::now();
    for (int b = 0; b < kBlocks; ++b) {
      cFilter.processBlock(buf.data(), buf.size());
    }
    auto t3 = std::chrono::steady_clock::now();
    g_sink += buf[0];
    crtpMs.push_back(std::chrono::duration<double, std::milli>(t3 - t2).count());
  }

  double vMed = median(virtMs);
  double cMed = median(crtpMs);
  std::printf("处理 %zu 个音频采样点（%zu/帧 × %d 帧，%d 轮中位数，-O2）：\n",
              kTotalSamples, kBlockSize, kBlocks, kRounds);
  std::printf("  虚函数逐采样点：%6.2f ms (%.2f ns/sample) | sizeof(VirtualGain) = %zu B\n",
              vMed, vMed * 1e6 / kTotalSamples, sizeof(VirtualGain));
  std::printf("  CRTP 静态多态 ：%6.2f ms (%.2f ns/sample) | sizeof(CrtpGain)    = %zu B\n",
              cMed, cMed * 1e6 / kTotalSamples, sizeof(CrtpGain));
  std::printf("  倍率对比      ：CRTP 提速 %.1fx | 对象体积降至 %.0f%%\n",
              vMed / cMed, 100.0 * sizeof(CrtpGain) / sizeof(VirtualGain));
}
