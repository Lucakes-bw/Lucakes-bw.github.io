// 《CRTP 奇异递归模板》：防呆惯用法——用私有构造 + friend Derived，在编译期拦住写错模板参数
#include <cstdio>

template <class Derived>
class SafeFilterBase {
 public:
  float apply(float x) const {
    return static_cast<const Derived*>(this)->applySample(x);
  }
 private:
  SafeFilterBase() = default;  // 构造函数设为 private
  friend Derived;              // 只有真正的 Derived 才能构造 SafeFilterBase<Derived>
};

struct MuteFilter : SafeFilterBase<MuteFilter> {
  float applySample(float) const { return 0.0f; }
};

#ifdef TRIGGER_WRONG_BASE
// 复制粘贴常见坑：忘了改模板参数，写成继承 SafeFilterBase<MuteFilter>！
// 因为 SafeFilterBase<MuteFilter> 的构造函数只对 friend MuteFilter 开放，
// GainFilter 无法调用其默认构造 → 直接在编译期报错，绝不留到运行时 UB！
struct GainFilter : SafeFilterBase<MuteFilter> {
  float gain = 2.0f;
  float applySample(float x) const { return x * gain; }
};
#else
struct GainFilter : SafeFilterBase<GainFilter> {
  float gain = 2.0f;
  float applySample(float x) const { return x * gain; }
};
#endif

int main() {
  MuteFilter m;
  GainFilter g;
  std::printf("Mute(0.5)=%.2f Gain(0.5)=%.2f\n", m.apply(0.5f), g.apply(0.5f));
}
