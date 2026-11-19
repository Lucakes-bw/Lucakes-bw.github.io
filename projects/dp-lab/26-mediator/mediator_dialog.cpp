// 《中介者模式》：每个控件只认识 DialogMediator，联动规则集中在 ExportDialog::changed() 一处
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

// 具体中介者：唯一知道所有控件的地方，集中维护对话框的联动规则
struct ExportDialog : DialogMediator {
  FormatSelect format{this};
  VideoPanel video{this};
  AudioPanel audio{this};
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

    if (video.enabled && (video.width <= 0 || video.height <= 0)) {
      btn.enabled = false; btn.hint = "分辨率必须大于 0";
    } else if (format.fmt == "mp4" && video.enabled && video.codec != "copy" &&
               (video.width % 2 != 0 || video.height % 2 != 0)) {
      btn.enabled = false; btn.hint = "h264 编码要求宽高都是偶数";
    } else {
      btn.enabled = true; btn.hint = "就绪";
    }
  }
};

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
        ExportDialog dlg;
        dlg.format.choose(f);
        dlg.video.chooseCodec(c);
        dlg.video.setSize(sz[0], sz[1]);

        Spec w = expected(f, c, sz[0], sz[1]);
        ++total;
        bool ok = (dlg.video.enabled == w.vEn && dlg.video.crfEnabled == w.crfEn &&
                   dlg.audio.enabled == w.aEn && dlg.btn.enabled == w.btnEn &&
                   (!w.vEn || dlg.video.codec == w.vCodec) && (!w.aEn || dlg.audio.codec == w.aCodec));
        if (!ok) {
          ++bad;
          std::printf("  ✗ fmt=%s codec=%s %dx%d\n", f, c, sz[0], sz[1]);
        }
      }
  std::printf("%d 种组合里 %d 种和规格不一致\n", total, bad);
}
