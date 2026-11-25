// 《Pimpl 惯用法》：现代 C++ Fast Pimpl——用栈上对齐缓冲区 + placement new 省掉一次堆分配
#include <cstddef>
#include <cstdio>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

// ================ FastPlayer.h（公开头文件：零重型依赖，零堆分配） ================
class FastPlayer {
 public:
  explicit FastPlayer(std::string url);
  ~FastPlayer();

  FastPlayer(FastPlayer&& other) noexcept;
  FastPlayer& operator=(FastPlayer&& other) noexcept;
  FastPlayer(const FastPlayer&) = delete;
  FastPlayer& operator=(const FastPlayer&) = delete;

  void seek(int ms);
  int positionMs() const;
  const std::string& url() const;

 private:
  struct Impl;                                     // 前向声明
  static constexpr std::size_t kStorageSize  = 64; // 预留足够容纳 Impl 的栈空间
  static constexpr std::size_t kStorageAlign = alignof(std::max_align_t);
  alignas(kStorageAlign) std::byte storage_[kStorageSize];

  Impl* impl() noexcept;
  const Impl* impl() const noexcept;
};

// ================ FastPlayer.cpp（实现文件） ================
struct FastPlayer::Impl {
  std::string url;
  int positionMs = 0;
  int volume = 80;
  bool playing = false;
  explicit Impl(std::string u) : url(std::move(u)) {}
};

FastPlayer::Impl* FastPlayer::impl() noexcept {
  return std::launder(reinterpret_cast<Impl*>(storage_));
}
const FastPlayer::Impl* FastPlayer::impl() const noexcept {
  return std::launder(reinterpret_cast<const Impl*>(storage_));
}

FastPlayer::FastPlayer(std::string url) {
  // 编译期守卫：一旦 Impl 膨胀超过缓冲区或对齐不满足，立刻报错提醒
  static_assert(sizeof(Impl) <= kStorageSize, "FastPlayer::Impl exceeds kStorageSize");
  static_assert(alignof(Impl) <= kStorageAlign, "FastPlayer::Impl alignment too strict");
  ::new (storage_) Impl(std::move(url));           // placement new：零堆分配
}
FastPlayer::~FastPlayer() {
  impl()->~Impl();                                 // 显式调用析构函数
}
FastPlayer::FastPlayer(FastPlayer&& other) noexcept {
  ::new (storage_) Impl(std::move(*other.impl()));
}
FastPlayer& FastPlayer::operator=(FastPlayer&& other) noexcept {
  if (this != &other) *impl() = std::move(*other.impl());
  return *this;
}

void FastPlayer::seek(int ms) { impl()->positionMs = ms < 0 ? 0 : ms; }
int FastPlayer::positionMs() const { return impl()->positionMs; }
const std::string& FastPlayer::url() const { return impl()->url; }

int main() {
  FastPlayer p("https://cdn.example.com/demo.mp4");
  p.seek(4200);
  FastPlayer p2 = std::move(p);
  std::printf("sizeof(FastPlayer)=%zu B, url=%s, pos=%d ms\n",
              sizeof(FastPlayer), p2.url().c_str(), p2.positionMs());
}
