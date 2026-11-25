#include "VideoPlayer.h"
#include <cstdio>

void renderUiBar(VideoPlayer& p) {
  std::printf("[UI] pos=%d ms\n", p.positionMs());
}
