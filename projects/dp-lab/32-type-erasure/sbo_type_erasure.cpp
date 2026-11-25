// 《类型擦除》：进阶优化——32 字节 SBO 栈缓冲区 + 手写 VTable 函数指针表，小对象 0 次堆分配
#include <array>
#include <cstddef>
#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

class SboDrawable {
  static constexpr std::size_t kBufSize  = 32;
  static constexpr std::size_t kBufAlign = alignof(std::max_align_t);

  struct VTable {
    void (*draw)(const void*);
    void (*copy)(const void* src, void* dst);
    void (*move)(void* src, void* dst) noexcept;
    void (*destroy)(void*) noexcept;
    bool isSmall;
  };

  template <typename T>
  static constexpr bool kFitsInSbo =
      sizeof(T) <= kBufSize && alignof(T) <= kBufAlign &&
      std::is_nothrow_move_constructible_v<T>;

  template <typename T>
  static const VTable* vtableFor() noexcept {
    if constexpr (kFitsInSbo<T>) {
      static const VTable vt{
          [](const void* p) { static_cast<const T*>(p)->draw(); },
          [](const void* s, void* d) { ::new (d) T(*static_cast<const T*>(s)); },
          [](void* s, void* d) noexcept { ::new (d) T(std::move(*static_cast<T*>(s))); },
          [](void* p) noexcept { static_cast<T*>(p)->~T(); },
          true};
      return &vt;
    } else {
      static const VTable vt{
          [](const void* p) { (*static_cast<T* const*>(p))->draw(); },
          [](const void* s, void* d) {
            auto* ptr = new T(**static_cast<T* const*>(s));
            ::new (d) T*(ptr);
          },
          [](void* s, void* d) noexcept {
            auto*& sp = *static_cast<T**>(s);
            ::new (d) T*(sp);
            sp = nullptr;
          },
          [](void* p) noexcept { delete *static_cast<T**>(p); },
          false};
      return &vt;
    }
  }

  alignas(kBufAlign) std::byte buf_[kBufSize]{};
  const VTable* vptr_ = nullptr;

 public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, SboDrawable>>>
  SboDrawable(T x) : vptr_(vtableFor<std::decay_t<T>>()) {
    using U = std::decay_t<T>;
    if constexpr (kFitsInSbo<U>) {
      ::new (buf_) U(std::move(x));
    } else {
      auto* ptr = new U(std::move(x));
      ::new (buf_) U*(ptr);
    }
  }

  SboDrawable(const SboDrawable& o) : vptr_(o.vptr_) {
    if (vptr_) vptr_->copy(o.buf_, buf_);
  }
  SboDrawable(SboDrawable&& o) noexcept : vptr_(o.vptr_) {
    if (vptr_) {
      vptr_->move(o.buf_, buf_);
      vptr_->destroy(o.buf_);
      o.vptr_ = nullptr;
    }
  }
  SboDrawable& operator=(SboDrawable o) noexcept {
    if (vptr_) vptr_->destroy(buf_);
    vptr_ = o.vptr_;
    if (vptr_) {
      vptr_->move(o.buf_, buf_);
      vptr_->destroy(o.buf_);
      o.vptr_ = nullptr;
    }
    return *this;
  }
  ~SboDrawable() {
    if (vptr_) vptr_->destroy(buf_);
  }

  void draw() const { vptr_->draw(buf_); }
  bool isSmall() const noexcept { return vptr_ && vptr_->isSmall; }
};

struct VideoLayer {                               // 8 B <= 32 B：走 SBO 内联
  int w, h;
  void draw() const { std::printf("  [Video] %dx%d\n", w, h); }
};

struct Watermark {                                // 16 B <= 32 B：走 SBO 内联
  const char* brand;
  double alpha;
  void draw() const { std::printf("  [Watermark] %s (alpha=%.1f)\n", brand, alpha); }
};

struct LutOverlay {                               // 64 B > 32 B：自动退回堆分配
  std::array<float, 16> matrix;
  void draw() const { std::printf("  [LutOverlay] 4x4 matrix (m0=%.2f)\n", matrix[0]); }
};

int main() {
  std::vector<SboDrawable> scene;
  scene.push_back(VideoLayer{1920, 1080});
  scene.push_back(Watermark{"LuckyStudio", 0.6});
  scene.push_back(LutOverlay{{1.25f}});

  std::printf("sizeof(SboDrawable) = %zu B\n", sizeof(SboDrawable));
  const char* names[] = {"VideoLayer (8 B)", "Watermark (16 B)", "LutOverlay (64 B)"};
  for (std::size_t i = 0; i < scene.size(); ++i) {
    std::printf("%-18s → %s\n", names[i], scene[i].isSmall() ? "SBO 栈内联 (0 次堆分配)" : "自动落堆");
    scene[i].draw();
  }
}
