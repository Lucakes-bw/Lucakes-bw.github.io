// 《CRTP 奇异递归模板》：Mixin 能力注入——对象存活计数 Counted<T> + 自动深拷贝 Cloneable<Derived, Base> + 比较运算符
#include <cstdio>
#include <memory>

// Mixin 1：按派生类独立统计存活对象数量（不同 T 实例化出各自独立的 static 计数器）
template <class Derived>
class Counted {
 public:
  static int alive() { return alive_; }
 protected:
  Counted() { ++alive_; }
  Counted(const Counted&) { ++alive_; }
  Counted(Counted&&) noexcept { ++alive_; }
  ~Counted() { --alive_; }
 private:
  inline static int alive_ = 0;  // C++17 inline static，每个 Derived 独享一份
};

// Mixin 2：结合运行时多态，自动生成 Prototype 模式的 clone()，省去每个子类重复写样板
struct ClipBase {
  virtual ~ClipBase() = default;
  virtual std::unique_ptr<ClipBase> clone() const = 0;
  virtual const char* kind() const = 0;
};

template <class Derived, class Base = ClipBase>
class Cloneable : public Base {
 public:
  std::unique_ptr<Base> clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived&>(*this));
  }
};

// Mixin 3：只需实现 == 和 <，自动派生 !=、<=、>、>=
template <class Derived>
struct Comparable {
  friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
  friend bool operator>(const Derived& a, const Derived& b)  { return b < a; }
  friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
  friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
};

class VideoClip : public Cloneable<VideoClip>, public Counted<VideoClip>, public Comparable<VideoClip> {
 public:
  explicit VideoClip(int frames) : frames_(frames) {}
  const char* kind() const override { return "VideoClip"; }
  int frames() const { return frames_; }
  bool operator==(const VideoClip& o) const { return frames_ == o.frames_; }
  bool operator<(const VideoClip& o) const  { return frames_ < o.frames_; }
 private:
  int frames_;
};

class AudioClip : public Cloneable<AudioClip>, public Counted<AudioClip> {
 public:
  const char* kind() const override { return "AudioClip"; }
};

int main() {
  VideoClip v1(120), v2(240);
  AudioClip a1;
  auto v1Copy = v1.clone();  // 自动生成的 clone()，零手写样板
  std::printf("alive: Video=%d Audio=%d | clone=%s\n",
              VideoClip::alive(), AudioClip::alive(), v1Copy->kind());
  std::printf("Comparable: v1!=v2:%d v2>v1:%d v1<=v2:%d\n",
              v1 != v2, v2 > v1, v1 <= v2);
  v1Copy.reset();
  std::printf("释放 clone 后 Video alive=%d\n", VideoClip::alive());
}
