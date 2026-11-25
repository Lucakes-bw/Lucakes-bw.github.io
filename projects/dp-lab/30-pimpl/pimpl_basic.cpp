// 《Pimpl 惯用法》：单文件完整可运行 Pimpl 示例（含移动语义与深拷贝）
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ================ VideoPlayer.h（公开头文件：零重型依赖） ================
class VideoPlayer {
 public:
  explicit VideoPlayer(std::string url);
  ~VideoPlayer();                                  // 只能声明，必须在 Impl 完整后再定义

  VideoPlayer(VideoPlayer&&) noexcept;             // 移动构造/赋值同样在 Impl 完整后定义
  VideoPlayer& operator=(VideoPlayer&&) noexcept;

  VideoPlayer(const VideoPlayer& other);           // 若需要拷贝，手写深拷贝
  VideoPlayer& operator=(const VideoPlayer& other);

  void play();
  void seek(int ms);
  void setVolume(int vol);
  int positionMs() const;
  const std::string& url() const;

 private:
  struct Impl;                                     // 不完整类型前向声明
  std::unique_ptr<Impl> pImpl_;                    // 固定占 1 个指针（64 位下恒为 8 B）
};

// ================ VideoPlayer.cpp（实现文件：隐藏全部重型依赖） ================
struct VideoPlayer::Impl {
  std::string url;
  int positionMs = 0;
  int volume = 80;
  bool playing = false;
  std::vector<int> frameQueue;                     // 模拟解码帧缓冲、网络句柄等重型内部状态

  explicit Impl(std::string u) : url(std::move(u)), frameQueue(16, 0) {}
};

VideoPlayer::VideoPlayer(std::string url)
    : pImpl_(std::make_unique<Impl>(std::move(url))) {}

VideoPlayer::~VideoPlayer() = default;
VideoPlayer::VideoPlayer(VideoPlayer&&) noexcept = default;
VideoPlayer& VideoPlayer::operator=(VideoPlayer&&) noexcept = default;

VideoPlayer::VideoPlayer(const VideoPlayer& other)
    : pImpl_(other.pImpl_ ? std::make_unique<Impl>(*other.pImpl_) : nullptr) {}

VideoPlayer& VideoPlayer::operator=(const VideoPlayer& other) {
  if (this != &other) {
    if (!other.pImpl_) {
      pImpl_.reset();
    } else if (pImpl_) {
      *pImpl_ = *other.pImpl_;                     // 复用已有堆块，免一次 new/delete
    } else {
      pImpl_ = std::make_unique<Impl>(*other.pImpl_);
    }
  }
  return *this;
}

void VideoPlayer::play() { pImpl_->playing = true; }
void VideoPlayer::seek(int ms) { pImpl_->positionMs = ms < 0 ? 0 : ms; }
void VideoPlayer::setVolume(int vol) { pImpl_->volume = vol < 0 ? 0 : (vol > 100 ? 100 : vol); }
int VideoPlayer::positionMs() const { return pImpl_->positionMs; }
const std::string& VideoPlayer::url() const { return pImpl_->url; }

int main() {
  VideoPlayer p1("rtsp://cam.local/live0");
  p1.play();
  p1.seek(12500);
  p1.setVolume(65);

  VideoPlayer p2 = p1;                             // 深拷贝：各自独立堆上 Impl
  p2.seek(30000);

  VideoPlayer p3 = std::move(p1);                  // 移动：O(1) 指针转移
  std::printf("sizeof(VideoPlayer) = %zu B\n", sizeof(VideoPlayer));
  std::printf("p3(原p1): url=%s pos=%d ms\n", p3.url().c_str(), p3.positionMs());
  std::printf("p2(深拷贝): url=%s pos=%d ms\n", p2.url().c_str(), p2.positionMs());
}
