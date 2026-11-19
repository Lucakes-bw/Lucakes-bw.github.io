// 《中介者模式》：问题版——四个控件两两直接持有指针，联动规则散落在每个控件里
#include <cstdio>
#include <string>

struct FormatSelect;
struct VideoPanel;
struct AudioPanel;
struct ExportButton;

struct FormatSelect {                 // 容器格式：mp4 / gif / wav
  std::string fmt = "mp4";
  VideoPanel* video = nullptr;
  AudioPanel* audio = nullptr;
  ExportButton* btn = nullptr;
  void choose(std::string f);
};

struct VideoPanel {                   // 视频编码参数：gif 固定用 gif 编码，wav 没有视频轨
  bool enabled = true;
  std::string codec = "h264";         // h264 / copy / gif
  int width = 1280, height = 720, crf = 23;
  bool crfEnabled = true;
  FormatSelect* format = nullptr;
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

struct ExportButton {                 // 导出按钮：所有开启的面板参数合法时才能点
  bool enabled = true;
  std::string hint = "就绪";
  FormatSelect* format = nullptr;
  VideoPanel* video = nullptr;
  AudioPanel* audio = nullptr;
  void refresh() {
    if (video->enabled && (video->width <= 0 || video->height <= 0)) {
      enabled = false; hint = "分辨率必须大于 0"; return;
    }
    if (format->fmt == "mp4" && video->enabled && video->codec != "copy" &&
        (video->width % 2 != 0 || video->height % 2 != 0)) {
      enabled = false; hint = "h264 编码要求宽高都是偶数"; return;
    }
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
  btn->refresh();
}

void VideoPanel::setSize(int w, int h) {
  width = w; height = h;
  btn->refresh();
}

// ---- 以下是测试：遍历「格式 × 视频编码 × 分辨率」的每种操作序列，和规格逐项对照
struct Spec {
  bool vEn, crfEn, aEn, btnEn;
  std::string vCodec, aCodec;
};

Spec expected(const std::string& fmt, const std::string& reqCodec, int w, int h) {
  Spec s{true, true, true, true, "h264", "aac"};
  if (fmt == "gif") {
    s.vCodec = "gif"; s.crfEn = false; s.aEn = false;
  } else if (fmt == "wav") {
    s.vEn = false; s.crfEn = false; s.aCodec = "pcm";
  } else {
    s.vCodec = (reqCodec == "gif") ? "h264" : reqCodec;
    s.crfEn = (s.vCodec == "h264");
  }
  if (s.vEn && (w <= 0 || h <= 0)) s.btnEn = false;
  if (fmt == "mp4" && s.vEn && s.vCodec != "copy" && (w % 2 != 0 || h % 2 != 0)) s.btnEn = false;
  return s;
}

int main() {
  const char* fmts[] = {"mp4", "gif", "wav"};
  const char* codecs[] = {"h264", "copy", "gif"};
  const int sizes[][2] = {{1280, 720}, {641, 360}, {0, 360}};
  int total = 0, bad = 0;
  for (const char* f : fmts)
    for (const char* c : codecs)
      for (auto& sz : sizes) {
        FormatSelect fmt; VideoPanel video; AudioPanel audio; ExportButton btn;
        fmt.video = &video; fmt.audio = &audio; fmt.btn = &btn;
        video.format = &fmt; video.btn = &btn;
        audio.format = &fmt; audio.video = &video; audio.btn = &btn;
        btn.format = &fmt; btn.video = &video; btn.audio = &audio;

        fmt.choose(f);
        video.chooseCodec(c);
        video.setSize(sz[0], sz[1]);

        Spec w = expected(f, c, sz[0], sz[1]);
        ++total;
        bool ok = (video.enabled == w.vEn && video.crfEnabled == w.crfEn &&
                   audio.enabled == w.aEn && btn.enabled == w.btnEn &&
                   (!w.vEn || video.codec == w.vCodec) && (!w.aEn || audio.codec == w.aCodec));
        if (!ok) {
          ++bad;
          std::printf("  ✗ fmt=%s codec=%s %dx%d\n", f, c, sz[0], sz[1]);
        }
      }
  std::printf("%d 种组合里 %d 种和规格不一致\n", total, bad);
}
