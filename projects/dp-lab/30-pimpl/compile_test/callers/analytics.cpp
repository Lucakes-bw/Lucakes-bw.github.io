#include "VideoPlayer.h"

std::size_t reportAnalytics(const VideoPlayer& p) {
  return p.url().size() + static_cast<std::size_t>(p.positionMs());
}
