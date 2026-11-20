// 《解释器模式》：问题版——用 FilterSpec 结构体堆布尔/阈值开关，遇到 or/not 嵌套组合就表达不出来
#include <cstdio>
#include <string>
#include <vector>

struct Clip {
  std::string name;
  int width, fps, dur;
  std::string codec;
  bool hdr;
};

struct FilterSpec {
  int minWidth = 0;
  int minFps = 0;
  int maxDur = 0;       // 0 表示不限
  std::string codec;    // 空串表示不限
  bool onlySdr = false;

  bool match(const Clip& c) const {
    if (minWidth > 0 && c.width < minWidth) return false;
    if (minFps > 0 && c.fps < minFps) return false;
    if (maxDur > 0 && c.dur >= maxDur) return false;
    if (!codec.empty() && c.codec != codec) return false;
    if (onlySdr && c.hdr) return false;
    return true;
  }
};

int main() {
  std::vector<Clip> clips = {
      {"intro_4k.mov",   3840, 60,  18, "hevc", true},
      {"interview.mp4",  1920, 30, 120, "h264", false},
      {"broll_slow.mp4", 1920, 60,  12, "h264", false},
      {"drone_hdr.mkv",  2560, 30,  25, "hevc", true},
      {"proxy_720.mp4",  1280, 24,   9, "h264", false},
  };

  // 需求 1：not hdr and dur < 30 —— 刚好全是 and，FilterSpec 勉强能写
  FilterSpec s1; s1.onlySdr = true; s1.maxDur = 30;
  std::printf("需求 1 (not hdr and dur < 30):");
  for (const auto& c : clips) if (s1.match(c)) std::printf(" %s", c.name.c_str());
  std::printf("\n");

  // 需求 2：(width >= 1920 and fps >= 60) or codec == "hevc" —— 单个 FilterSpec 表达不了，只能拆成两条手写 ||
  FilterSpec a; a.minWidth = 1920; a.minFps = 60;
  FilterSpec b; b.codec = "hevc";
  std::printf("需求 2 ((width>=1920 and fps>=60) or codec==\"hevc\"):");
  for (const auto& c : clips) if (a.match(c) || b.match(c)) std::printf(" %s", c.name.c_str());
  std::printf("\n");
}
