#include "VideoPlayer.h"
#include <cstdio>

void renderUiBar(VideoPlayer& p);
int switchNextItem(VideoPlayer& p);
int syncSubtitleTrack(const VideoPlayer& p);
int fetchDanmakuBucket(const VideoPlayer& p);
std::size_t reportAnalytics(const VideoPlayer& p);

int main() {
  VideoPlayer p("rtsp://cam.local/live0");
  switchNextItem(p);
  p.seek(12000);
  renderUiBar(p);
  std::printf("sizeof(VideoPlayer)=%zu B, sub=%d, dm=%d, stat=%zu\n",
              sizeof(VideoPlayer), syncSubtitleTrack(p),
              fetchDanmakuBucket(p), reportAnalytics(p));
}
