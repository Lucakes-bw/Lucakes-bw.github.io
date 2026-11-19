// 《中介者模式》练习 2：给 ExportDialog 加一个 PresetSelect（「自定义 / 朋友圈小视频 / 仅提取音频」）
#include <cstdio>
#include <string>

struct Widget;
struct DialogMediator {
  virtual ~DialogMediator() = default;
  virtual void changed(Widget* sender) = 0;
};

struct Widget {
  DialogMediator* med = nullptr;
  explicit Widget(DialogMediator* m) : med(m) {}
  void notify() { if (med) med->changed(this); }
};

struct PresetSelect : Widget {                // 新增控件：选预设一键改好其它控件，手动改参数时自动退回「自定义」
  std::string preset = "custom";              // custom / moments / audio_only
  using Widget::Widget;
  void choose(std::string p) { preset = std::move(p); notify(); }
};

struct FormatSelect : Widget {
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
  void setCrf(int q) { crf = q; notify(); }
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

struct ExportDialog : DialogMediator {
  PresetSelect preset{this};
  FormatSelect format{this};
  VideoPanel video{this};
  AudioPanel audio{this};
  ExportButton btn{this};

  void changed(Widget* sender) override {
    if (sender == &preset) {                  // 用户选了预设：由中介者填入一整套参数
      if (preset.preset == "moments") {
        format.fmt = "mp4";
        video.codec = "h264"; video.width = 720; video.height = 1280; video.crf = 25;
        audio.codec = "aac";
      } else if (preset.preset == "audio_only") {
        format.fmt = "wav";
      }
    } else {                                  // 用户手动改了任何参数：预设退回「自定义」
      preset.preset = "custom";
    }

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

  void dump(const char* tag) const {
    std::printf("%-16s → preset=%-10s fmt=%-3s %dx%d crf=%d audio=%s(%s)\n",
                tag, preset.preset.c_str(), format.fmt.c_str(),
                video.width, video.height, video.crf,
                audio.enabled ? "开" : "关", audio.codec.c_str());
  }
};

int main() {
  ExportDialog dlg;
  dlg.dump("1. 默认");
  dlg.preset.choose("moments");
  dlg.dump("2. 选朋友圈预设");
  dlg.video.setCrf(20);                       // 手动调了一项参数，预设自动跳回 custom
  dlg.dump("3. 手动改 CRF=20");
  dlg.preset.choose("audio_only");
  dlg.dump("4. 选仅提取音频");
}
