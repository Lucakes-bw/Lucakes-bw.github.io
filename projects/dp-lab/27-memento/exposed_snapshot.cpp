// 《备忘录模式》：问题版——为了做撤销把 ColorGrader 内部字段全部暴露，导致漏存私有状态且破坏不变式
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct ColorGrader {
  // 为了让 History 能读写状态，被迫全部 public（破坏封装）
  int brightness = 0;       // 合法范围 [-100, 100]
  int contrast   = 100;     // 合法范围 [0, 200]
  int saturation = 100;     // 合法范围 [0, 200]
  std::string lutName = "None";

  // 内部私有语义的字段：渲染缓存脏标记与状态校验和
  bool cacheDirty = true;
  int checksum = 200;       // 不变式：checksum == brightness + contrast + saturation

  void setBrightness(int v) {
    brightness = std::clamp(v, -100, 100);
    checksum = brightness + contrast + saturation;
    cacheDirty = true;
  }
  void setLut(std::string name) {
    lutName = std::move(name);
    cacheDirty = true;
  }
  void render() { cacheDirty = false; }  // 渲染一帧后，GPU 缓存与当前参数同步
  bool invariantOk() const {
    return brightness >= -100 && brightness <= 100 &&
           checksum == brightness + contrast + saturation;
  }
};

// 外部历史记录只能手工抄字段，很容易漏掉 cacheDirty / checksum
struct RawBackup {
  int brightness, contrast, saturation;
  std::string lutName;
};

int main() {
  ColorGrader g;
  std::vector<RawBackup> history;

  g.setBrightness(20);
  g.setLut("TealOrange");
  g.render();  // 画面已按 (+20, TealOrange) 渲染，cacheDirty = false
  history.push_back({g.brightness, g.contrast, g.saturation, g.lutName});

  g.setBrightness(-40);
  g.setLut("Kodak2383");
  g.render();  // 画面已按 (-40, Kodak2383) 渲染，cacheDirty = false

  // 坑 ①：外部按裸字段恢复，忘了更新 cacheDirty 和 checksum
  const auto& b = history.back();
  g.brightness = b.brightness;
  g.contrast   = b.contrast;
  g.saturation = b.saturation;
  g.lutName    = b.lutName;
  std::printf("撤销后：bri=%d lut=%s cacheDirty=%s invariantOk=%s\n",
              g.brightness, g.lutName.c_str(),
              g.cacheDirty ? "true" : "false",
              g.invariantOk() ? "true" : "false");

  // 坑 ②：字段 public 后，业务代码直接写入越界值，绕过 clamp 与 checksum
  g.brightness = 250;
  std::printf("误写后：bri=%d invariantOk=%s\n",
              g.brightness, g.invariantOk() ? "true" : "false");
}
