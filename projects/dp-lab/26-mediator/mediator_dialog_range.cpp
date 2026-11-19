// 《中介者模式》：加「只导出片段」面板——四个已有控件一行不改，只写 RangePanel 并更新中介者
#include <cstdio>
#include <string>

struct Widget;
struct DialogMediator {
  virtual ~DialogMediator() = default;
  virtual void changed(Widget* sender) = 0;
};

struct Widget {                               // 同事基类：只保存中介者指针
  DialogMediator* med = nullptr;
  explicit Widget(DialogMediator* m) : med(m) {}
  void notify() { if (med) med->changed(this); }
};

struct FormatSelect : Widget {                // 不认识 VideoPanel、AudioPanel、ExportButton
  std::string fmt = "mp4";
  using Widget::Widget;
  void choose(std::string f) { fmt = std::move(f); notify(); }
};

struct VideoPanel : Widget {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720, crf = 23;
  using Widget::Widget;
  void chooseCodec(std::string c) { if (enabled) { codec = std::move(c); notify(); } }
  void setSize(int w, int h) { width = w; height = h; notify(); }
};

struct AudioPanel : Widget {
  bool enabled = true;
  std::string codec = "aac";
  using Widget::Widget;
};

struct ExportButton : Widget {
  bool enabled = true;
  std::string hint = "就绪";
  using Widget::Widget;
};

struct RangePanel : Widget {                  // 新增同事：也只认识 DialogMediator
  bool clipOnly = false;
  double start = 0.0, end = 10.0;
  std::string note = "完整视频";
  using Widget::Widget;
  void setRange(bool on, double s, double e) {
    clipOnly = on; start = s; end = e;
    notify();
  }
};

// 具体中介者：唯一知道所有控件的地方，集中维护对话框的联动规则
struct ExportDialog : DialogMediator {
  FormatSelect format{this};
  VideoPanel video{this};
  AudioPanel audio{this};
  RangePanel range{this};
  ExportButton btn{this};

  void changed(Widget*) override {            // 无论哪个控件变了，都按同一套规则算出一致的状态
    if (format.fmt == "mp4") {
      video.enabled = true;
      if (video.codec == "gif") video.codec = "h264";
      video.crfEnabled = (video.codec == "h264");
      audio.enabled = true;
      if (audio.codec == "pcm") audio.codec = "aac";
    } else if (format.fmt == "gif") {
      video.enabled = true;
      video.codec = "gif";
      video.crfEnabled = false;
      audio.enabled = false;
    } else if (format.fmt == "wav") {
      video.enabled = false;
      video.crfEnabled = false;
      audio.enabled = true;
      audio.codec = "pcm";
    }

    if (!range.clipOnly) {
      range.note = "完整视频";
    } else if (video.enabled && video.codec == "copy") {
      range.note = "流拷贝只能从关键帧切";
    } else {
      range.note = "精确裁剪";
    }

    if (video.enabled && (video.width <= 0 || video.height <= 0)) {
      btn.enabled = false; btn.hint = "分辨率必须大于 0";
    } else if (format.fmt == "mp4" && video.enabled && video.codec != "copy" &&
               (video.width % 2 != 0 || video.height % 2 != 0)) {
      btn.enabled = false; btn.hint = "h264 编码要求宽高都是偶数";
    } else if (range.clipOnly && range.end <= range.start) {
      btn.enabled = false; btn.hint = "结束时间必须大于开始时间";
    } else if (format.fmt == "gif" && range.clipOnly && (range.end - range.start) > 15.0) {
      btn.enabled = false; btn.hint = "GIF 片段不能超过 15 秒";
    } else {
      btn.enabled = true; btn.hint = "就绪";
    }
  }
};

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
          ExportDialog dlg;
          dlg.format.choose(f);
          dlg.range.setRange(r.on, r.s, r.e);
          dlg.video.chooseCodec(c);
          dlg.video.setSize(sz[0], sz[1]);

          Spec w = expected(f, c, sz[0], sz[1], r.on, r.s, r.e);
          ++total;
          bool ok = (dlg.video.enabled == w.vEn && dlg.video.crfEnabled == w.crfEn &&
                     dlg.audio.enabled == w.aEn && dlg.btn.enabled == w.btnEn &&
                     dlg.range.note == w.note &&
                     (!w.vEn || dlg.video.codec == w.vCodec) && (!w.aEn || dlg.audio.codec == w.aCodec));
          if (!ok) {
            ++bad;
            std::printf("  ✗ fmt=%s range=%s codec=%s %dx%d\n", f, r.tag, c, sz[0], sz[1]);
          }
        }
  std::printf("%d 种组合里 %d 种和规格不一致\n", total, bad);
}
