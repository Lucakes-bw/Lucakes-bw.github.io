#include "VideoPlayer.h"
#include "../heavy_deps.h"

struct VideoPlayer::Impl {
  DecoderContext decoder;
  RenderPipeline renderer;
  NetworkSession net;
  int positionMs = 0;
  bool playing = false;
#ifdef ADD_PRIVATE_FIELD
  int retryCount = 3; // 模拟迭代：新增私有成员完全不影响公开头文件！
#endif
  explicit Impl(std::string url) { net.url = std::move(url); }
};

VideoPlayer::VideoPlayer(std::string url)
    : pImpl_(std::make_unique<Impl>(std::move(url))) {}
VideoPlayer::~VideoPlayer() = default;
VideoPlayer::VideoPlayer(VideoPlayer&&) noexcept = default;
VideoPlayer& VideoPlayer::operator=(VideoPlayer&&) noexcept = default;

void VideoPlayer::play() { pImpl_->playing = true; }
void VideoPlayer::seek(int ms) { pImpl_->positionMs = ms < 0 ? 0 : ms; }
int VideoPlayer::positionMs() const { return pImpl_->positionMs; }
std::string VideoPlayer::url() const { return pImpl_->net.url; }
