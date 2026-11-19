// 《中介者模式》：现代 C++ 写法——控件不继承基类，只暴露 std::function 回调；中介者拥有控件或在装配处用 lambda 连接
#include <cstdio>
#include <functional>
#include <string>

struct FormatSelect {                 // 纯独立组件：不知道任何中介者基类，只提供一个 onChange 回调槽
  std::string fmt = "mp4";
  std::function<void()> onChange;
  void choose(std::string f) {
    fmt = std::move(f);
    if (onChange) onChange();
  }
};

struct VideoPanel {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720, crf = 23;
  std::function<void()> onChange;
  void chooseCodec(std::string c) {
    if (!enabled) return;
    codec = std::move(c);
    if (onChange) onChange();
  }
  void setSize(int w, int h) {
    width = w; height = h;
    if (onChange) onChange();
  }
};

struct AudioPanel {
  bool enabled = true;
  std::string codec = "aac";
};

struct ExportButton {
  bool enabled = true;
  std::string hint = "就绪";
};

struct ExportDialog {                 // 中介者：用组合直接拥有 4 个控件，构造函数里把回调绑到 sync()
  FormatSelect format;
  VideoPanel video;
  AudioPanel audio;
  ExportButton btn;

  ExportDialog() {
    format.onChange = [this] { sync(); };
    video.onChange  = [this] { sync(); };
  }
  ExportDialog(const ExportDialog&) = delete;
  ExportDialog& operator=(const ExportDialog&) = delete;

  void sync() {
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

  void dump(const char* step) const {
    std::printf("%-18s → fmt=%-3s video=%s(%s,crf=%s) audio=%s(%s) btn=%s(%s)\n",
                step, format.fmt.c_str(),
                video.enabled ? "开" : "关", video.codec.c_str(), video.crfEnabled ? "开" : "关",
                audio.enabled ? "开" : "关", audio.codec.c_str(),
                btn.enabled ? "可点" : "禁用", btn.hint.c_str());
  }
};

int main() {
  ExportDialog dlg;
  dlg.dump("1. 初始状态");
  dlg.format.choose("gif");
  dlg.dump("2. 格式切到 gif");
  dlg.format.choose("mp4");
  dlg.dump("3. 格式切回 mp4");
  dlg.video.chooseCodec("copy");
  dlg.dump("4. 视频选 copy");
  dlg.video.setSize(641, 360);
  dlg.dump("5. 改奇数宽(copy)");
  dlg.video.chooseCodec("h264");
  dlg.dump("6. 切回 h264");
}
