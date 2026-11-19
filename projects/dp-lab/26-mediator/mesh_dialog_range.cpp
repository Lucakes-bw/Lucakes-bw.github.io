// 《中介者模式》：网状版加「只导出片段」面板——要改 4 个旧控件，还漏了两处联动
#include <cstdio>
#include <string>

struct FormatSelect;
struct VideoPanel;
struct AudioPanel;
struct RangePanel;
struct ExportButton;

struct FormatSelect {                 // 容器格式：mp4 / gif / wav
  std::string fmt = "mp4";
  VideoPanel* video = nullptr;
  AudioPanel* audio = nullptr;
  RangePanel* range = nullptr;        // 新增：切到 gif 时如果区间超 15 秒要锁住按钮
  ExportButton* btn = nullptr;
  void choose(std::string f);
};

struct VideoPanel {                   // 视频编码参数：gif 固定用 gif 编码，wav 没有视频轨
  bool enabled = true;
  std::string codec = "h264";         // h264 / copy / gif
  int width = 1280, height = 720, crf = 23;
  bool crfEnabled = true;
  FormatSelect* format = nullptr;
  RangePanel* range = nullptr;        // 新增：选了片段时若当前是 copy，只能精确到关键帧
  ExportButton* btn = nullptr;
  void chooseCodec(std::string c);
  void setSize(int w, int h);
};

struct AudioPanel {                   // 音频编码参数：gif 没有音频轨
  bool enabled = true;
  std::string codec = "aac";          // aac / pcm / copy
  FormatSelect* format = nullptr;
  VideoPanel* video = nullptr;
  ExportButton* btn = nullptr;
};

struct RangePanel {                   // 新增：只导出 [start, end] 这一段
  bool clipOnly = false;
  double start = 0.0, end = 10.0;
  std::string note = "完整视频";
  FormatSelect* format = nullptr;
  VideoPanel* video = nullptr;
  ExportButton* btn = nullptr;
  void setRange(bool on, double s, double e);
};

struct ExportButton {                 // 导出按钮：所有开启的面板参数合法时才能点
  bool enabled = true;
  std::string hint = "就绪";
  FormatSelect* format = nullptr;
  VideoPanel* video = nullptr;
  AudioPanel* audio = nullptr;
  RangePanel* range = nullptr;        // 新增
  void refresh() {
    if (video->enabled && (video->width <= 0 || video->height <= 0)) {
      enabled = false; hint = "分辨率必须大于 0"; return;
    }
    if (format->fmt == "mp4" && video->enabled && video->codec != "copy" &&
        (video->width % 2 != 0 || video->height % 2 != 0)) {
      enabled = false; hint = "h264 编码要求宽高都是偶数"; return;
    }
    if (range->clipOnly && range->end <= range->start) {
      enabled = false; hint = "结束时间必须大于开始时间"; return;
    }
    // 漏改 1：GIF 选了片段时，时长 (end - start) 不能超过 15 秒，这里忘了检查
    enabled = true; hint = "就绪";
  }
};

void FormatSelect::choose(std::string f) {
  fmt = std::move(f);
  if (fmt == "mp4") {
    video->enabled = true;
    if (video->codec == "gif") video->codec = "h264";
    video->crfEnabled = (video->codec != "copy");
    audio->enabled = true;
    if (audio->codec == "pcm") audio->codec = "aac";
  } else if (fmt == "gif") {          // GIF 没有音频轨，视频固定用 gif 编码、不用 CRF
    video->enabled = true;
    video->codec = "gif";
    video->crfEnabled = false;
    audio->enabled = false;
  } else if (fmt == "wav") {          // WAV 只有音频轨，默认用未压缩的 pcm
    video->enabled = false;
    video->crfEnabled = false;
    audio->enabled = true;
    audio->codec = "pcm";
  }
  btn->refresh();
}

void VideoPanel::chooseCodec(std::string c) {
  if (!enabled) return;
  if (format->fmt == "gif") c = "gif";
  else if (c == "gif") c = "h264";
  codec = std::move(c);
  crfEnabled = (codec == "h264");     // copy 和 gif 都不需要设 CRF
  // 漏改 2：先开了 clipOnly 再把编码切到 copy 时，忘了更新 range->note 的「关键帧裁剪」提示
  btn->refresh();
}

void VideoPanel::setSize(int w, int h) {
  width = w; height = h;
  btn->refresh();
}

void RangePanel::setRange(bool on, double s, double e) {
  clipOnly = on; start = s; end = e;
  if (!clipOnly) {
    note = "完整视频";
  } else if (video->enabled && video->codec == "copy") {
    note = "流拷贝只能从关键帧切";
  } else {
    note = "精确裁剪";
  }
  btn->refresh();
}

// ---- 以下是测试：遍历「格式 × 视频编码 × 分辨率 × 片段区间」的每种组合，和规格逐项对照
struct Spec {
  bool vEn, crfEn, aEn, btnEn;
  std::string vCodec, aCodec, note;
};

Spec expected(const std::string& fmt, const std::string& reqCodec, int w, int h,
              bool clip, double s, double e) {
  Spec sp{true, true, true, true, "h264", "aac", "完整视频"};
  if (fmt == "gif") {
    sp.vCodec = "gif"; sp.crfEn = false; sp.aEn = false;
  } else if (fmt == "wav") {
    sp.vEn = false; sp.crfEn = false; sp.aCodec = "pcm";
  } else {
    sp.vCodec = (reqCodec == "gif") ? "h264" : reqCodec;
    sp.crfEn = (sp.vCodec == "h264");
  }
  if (clip) {
    sp.note = (sp.vEn && sp.vCodec == "copy") ? "流拷贝只能从关键帧切" : "精确裁剪";
  }
  if (sp.vEn && (w <= 0 || h <= 0)) sp.btnEn = false;
  if (fmt == "mp4" && sp.vEn && sp.vCodec != "copy" && (w % 2 != 0 || h % 2 != 0)) sp.btnEn = false;
  if (clip && e <= s) sp.btnEn = false;
  if (fmt == "gif" && clip && (e - s) > 15.0) sp.btnEn = false;
  return sp;
}

int main() {
  const char* fmts[] = {"mp4", "gif", "wav"};
  const char* codecs[] = {"h264", "copy"};
  const int sizes[][2] = {{1280, 720}, {641, 360}};
  struct Rng { bool on; double s, e; const char* tag; } rngs[] = {
      {false, 0, 10, "全程"}, {true, 2, 8, "2~8s"}, {true, 0, 20, "0~20s"}, {true, 5, 3, "5~3s"}};
  int total = 0, bad = 0;
  for (const char* f : fmts)
    for (auto& r : rngs)
      for (const char* c : codecs)
        for (auto& sz : sizes) {
          FormatSelect fmt; VideoPanel video; AudioPanel audio; RangePanel range; ExportButton btn;
          fmt.video = &video; fmt.audio = &audio; fmt.range = &range; fmt.btn = &btn;
          video.format = &fmt; video.range = &range; video.btn = &btn;
          audio.format = &fmt; audio.video = &video; audio.btn = &btn;
          range.format = &fmt; range.video = &video; range.btn = &btn;
          btn.format = &fmt; btn.video = &video; btn.audio = &audio; btn.range = &range;

          fmt.choose(f);
          range.setRange(r.on, r.s, r.e);     // 用户先勾上片段，再去切编码器
          video.chooseCodec(c);
          video.setSize(sz[0], sz[1]);

          Spec w = expected(f, c, sz[0], sz[1], r.on, r.s, r.e);
          ++total;
          bool ok = (video.enabled == w.vEn && video.crfEnabled == w.crfEn &&
                     audio.enabled == w.aEn && btn.enabled == w.btnEn &&
                     range.note == w.note &&
                     (!w.vEn || video.codec == w.vCodec) && (!w.aEn || audio.codec == w.aCodec));
          if (!ok) {
            ++bad;
            if (bad <= 4)
              std::printf("  ✗ fmt=%s range=%s codec=%s %dx%d：btn=%d(期望%d) note=\"%s\"(期望\"%s\")\n",
                          f, r.tag, c, sz[0], sz[1], btn.enabled, w.btnEn,
                          range.note.c_str(), w.note.c_str());
          }
        }
  std::printf("%d 种组合里 %d 种和规格不一致\n", total, bad);
}
