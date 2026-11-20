// 《备忘录模式》：现代 C++——用 std::shared_ptr<const LutTable> 共享不可变大对象（COW 备忘录）
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct LutTable {
  std::string name;
  std::vector<float> data;  // 65536 个 float = 256 KB
  explicit LutTable(std::string n) : name(std::move(n)), data(65536, 1.0f) {}
};

class ColorGrader {
 public:
  class Snapshot {
   public:
    const std::string& label() const { return label_; }
    long lutRefCount() const { return lut_.use_count(); }
   private:
    friend class ColorGrader;
    Snapshot(std::string label, int b, int c, int s, std::shared_ptr<const LutTable> lut)
        : label_(std::move(label)), brightness_(b), contrast_(c),
          saturation_(s), lut_(std::move(lut)) {}
    std::string label_;
    int brightness_, contrast_, saturation_;
    std::shared_ptr<const LutTable> lut_;  // 关键：指向 const，多个快照安全共享
  };

  ColorGrader() : lut_(std::make_shared<const LutTable>("None")) {}

  void setBrightness(int v) { brightness_ = std::clamp(v, -100, 100); }
  void setContrast(int v)   { contrast_   = std::clamp(v, 0, 200); }
  void setLut(std::shared_ptr<const LutTable> lut) { lut_ = std::move(lut); }

  Snapshot save(std::string label) const {
    return Snapshot(std::move(label), brightness_, contrast_, saturation_, lut_);
  }
  void restore(const Snapshot& s) {
    brightness_ = s.brightness_;
    contrast_   = s.contrast_;
    saturation_ = s.saturation_;
    lut_        = s.lut_;
  }
  void dump(const char* tag) const {
    std::printf("%-10s → bri=%+3d con=%3d lut=%-10s (use_count=%ld)\n",
                tag, brightness_, contrast_, lut_->name.c_str(), lut_.use_count());
  }

 private:
  int brightness_ = 0, contrast_ = 100, saturation_ = 100;
  std::shared_ptr<const LutTable> lut_;
};

int main() {
  ColorGrader g;
  std::vector<ColorGrader::Snapshot> history;

  auto teal = std::make_shared<const LutTable>("TealOrange");
  g.setLut(teal);
  history.push_back(g.save("加载 TealOrange"));

  // 连续微调亮度/对比度并存档：256 KB LUT 零拷贝，只增引用计数
  g.setBrightness(10); history.push_back(g.save("亮度 +10"));
  g.setContrast(120);  history.push_back(g.save("对比度 120"));
  g.dump("微调后");

  // 切换新 LUT：替换指针，旧快照里的 TealOrange 丝毫不受影响
  g.setLut(std::make_shared<const LutTable>("Kodak2383"));
  g.setBrightness(-15);
  g.dump("切换 LUT");

  // 撤销回上一份快照：指针直接指回共享的 TealOrange
  g.restore(history.back());
  g.dump("恢复快照");
  std::printf("history[0] 与 history[2] 共享同一份 LUT，引用计数=%ld\n",
              history[0].lutRefCount());
}
