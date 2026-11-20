// 《备忘录模式》：经典写法——Snapshot 私有构造 + friend class ColorGrader，History 只能存取不透明快照
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

class ColorGrader {
 public:
  // Memento（备忘录）：窄接口对外部只暴露 label()，宽接口仅对友元 ColorGrader 开放
  class Snapshot {
   public:
    const std::string& label() const { return label_; }
   private:
    friend class ColorGrader;
    Snapshot(std::string label, int b, int c, int s, std::string lut, int rev, int sum)
        : label_(std::move(label)), brightness_(b), contrast_(c),
          saturation_(s), lutName_(std::move(lut)), revision_(rev), checksum_(sum) {}
    std::string label_;
    int brightness_, contrast_, saturation_;
    std::string lutName_;
    int revision_, checksum_;
  };

  void setBrightness(int v) { brightness_ = std::clamp(v, -100, 100); touch(); }
  void setContrast(int v)   { contrast_   = std::clamp(v, 0, 200);    touch(); }
  void setLut(std::string n){ lutName_    = std::move(n);             touch(); }
  void render()             { cacheDirty_ = false; }

  Snapshot save(std::string label) const {
    return Snapshot(std::move(label), brightness_, contrast_, saturation_,
                    lutName_, revision_, checksum_);
  }
  void restore(const Snapshot& s) {
    brightness_ = s.brightness_;
    contrast_   = s.contrast_;
    saturation_ = s.saturation_;
    lutName_    = s.lutName_;
    revision_   = s.revision_;
    checksum_   = s.checksum_;
    cacheDirty_ = true;  // 恢复参数后必然标脏，强制下一帧重绘
  }
  void dump(const char* tag) const {
    std::printf("%-10s → bri=%+3d con=%3d lut=%-10s rev=%d dirty=%s ok=%s\n",
                tag, brightness_, contrast_, lutName_.c_str(), revision_,
                cacheDirty_ ? "true" : "false",
                (checksum_ == brightness_ + contrast_ + saturation_) ? "true" : "false");
  }

 private:
  void touch() {
    ++revision_;
    checksum_ = brightness_ + contrast_ + saturation_;
    cacheDirty_ = true;
  }
  int brightness_ = 0, contrast_ = 100, saturation_ = 100;
  std::string lutName_ = "None";
  int revision_ = 0, checksum_ = 200;
  bool cacheDirty_ = true;
};

// Caretaker（负责人）：只管栈，看不到也不能改 Snapshot 的任何调色字段
class History {
 public:
  void backup(ColorGrader::Snapshot s) {
    undoStack_.push_back(std::move(s));
    redoStack_.clear();
  }
  bool undo(ColorGrader& g) {
    if (undoStack_.empty()) return false;
    redoStack_.push_back(g.save("重做点"));
    g.restore(undoStack_.back());
    undoStack_.pop_back();
    return true;
  }
  bool redo(ColorGrader& g) {
    if (redoStack_.empty()) return false;
    undoStack_.push_back(g.save("撤销点"));
    g.restore(redoStack_.back());
    redoStack_.pop_back();
    return true;
  }
 private:
  std::vector<ColorGrader::Snapshot> undoStack_, redoStack_;
};

int main() {
  ColorGrader g;
  History h;
  g.render();

  h.backup(g.save("初始"));
  g.setBrightness(20); g.setLut("TealOrange"); g.render();
  g.dump("调色 1");

  h.backup(g.save("调色 1"));
  g.setBrightness(-40); g.setLut("Kodak2383"); g.render();
  g.dump("调色 2");

  h.undo(g); g.dump("撤销 1 次");
  h.undo(g); g.dump("撤销 2 次");
  h.redo(g); g.dump("重做 1 次");
}
