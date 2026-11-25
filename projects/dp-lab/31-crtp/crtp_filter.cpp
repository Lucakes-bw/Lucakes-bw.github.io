// 《CRTP 奇异递归模板》：静态多态——FilterBase<Derived> 用 static_cast 在编译期绑定子类，零 vptr 且可完全内联
#include <algorithm>
#include <cstdio>
#include <vector>

template <class Derived>
class FilterBase {
 public:
  // 公共骨架写在基类，热循环内通过 static_cast 直接调用子类方法（编译期决议，可内联+向量化）
  float processBuffer(std::vector<float>& pcm) const {
    const auto& self = static_cast<const Derived&>(*this);
    float peak = 0.0f;
    for (float& s : pcm) {
      s = std::clamp(self.applySample(s), -1.0f, 1.0f);
      peak = std::max(peak, s < 0 ? -s : s);
    }
    return peak;
  }

  const char* filterName() const {
    return static_cast<const Derived*>(this)->name();
  }

 protected:
  FilterBase() = default;   // 防止外部裸实例化基类
  ~FilterBase() = default;  // 无需虚析构：不通过基类指针 delete
};

class GainFilter : public FilterBase<GainFilter> {
 public:
  explicit GainFilter(float gain) : gain_(gain) {}
  const char* name() const { return "Gain"; }
  float applySample(float x) const { return x * gain_; }
 private:
  float gain_;
};

class SoftClipFilter : public FilterBase<SoftClipFilter> {
 public:
  explicit SoftClipFilter(float drive) : drive_(drive) {}
  const char* name() const { return "SoftClip"; }
  float applySample(float x) const {
    float y = x * drive_;
    return y / (1.0f + (y < 0 ? -y : y));
  }
 private:
  float drive_;
};

// 通用算法用模板接收任意 FilterBase<D>
template <class D>
void runFilter(const FilterBase<D>& f, std::vector<float>& pcm) {
  float peak = f.processBuffer(pcm);
  std::printf("[%s] peak=%.3f first=%.3f\n", f.filterName(), peak, pcm[0]);
}

int main() {
  std::vector<float> pcm = {0.2f, -0.5f, 0.8f, -0.9f};
  GainFilter gain(1.5f);
  SoftClipFilter clip(2.0f);

  runFilter(gain, pcm);
  runFilter(clip, pcm);
  std::printf("sizeof(GainFilter)=%zu B (无 vptr，纯 float 大小)\n", sizeof(GainFilter));
}
