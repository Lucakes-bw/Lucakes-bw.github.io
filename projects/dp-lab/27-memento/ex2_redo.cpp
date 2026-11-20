// 《备忘录模式》练习 2 参考：有界撤销栈（std::deque）+ Undo/Redo + 新编辑清空 Redo 栈
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

class ColorGrader {
 public:
  class Snapshot {
   public:
    const std::string& label() const { return label_; }
   private:
    friend class ColorGrader;
    Snapshot(std::string l, int b, int c) : label_(std::move(l)), brightness_(b), contrast_(c) {}
    std::string label_;
    int brightness_, contrast_;
  };
  void set(int b, int c) { brightness_ = b; contrast_ = c; }
  Snapshot save(std::string l) const { return Snapshot(std::move(l), brightness_, contrast_); }
  void restore(const Snapshot& s) { brightness_ = s.brightness_; contrast_ = s.contrast_; }
  void dump(const char* tag) const {
    std::printf("%-10s → bri=%+3d con=%3d\n", tag, brightness_, contrast_);
  }
 private:
  int brightness_ = 0, contrast_ = 100;
};

class BoundedHistory {
 public:
  explicit BoundedHistory(size_t maxDepth) : maxDepth_(maxDepth) {}

  void backup(ColorGrader::Snapshot s) {
    if (undo_.size() == maxDepth_) undo_.pop_front();  // 超出上限，丢弃最老快照
    undo_.push_back(std::move(s));
    redo_.clear();                                     // 新编辑使旧重做分支失效
  }
  bool undo(ColorGrader& g) {
    if (undo_.empty()) return false;
    redo_.push_back(g.save("redo"));
    g.restore(undo_.back());
    undo_.pop_back();
    return true;
  }
  bool redo(ColorGrader& g) {
    if (redo_.empty()) return false;
    if (undo_.size() == maxDepth_) undo_.pop_front();
    undo_.push_back(g.save("undo"));
    g.restore(redo_.back());
    redo_.pop_back();
    return true;
  }
  size_t undoSize() const { return undo_.size(); }
  size_t redoSize() const { return redo_.size(); }

 private:
  size_t maxDepth_;
  std::deque<ColorGrader::Snapshot> undo_;
  std::vector<ColorGrader::Snapshot> redo_;
};

int main() {
  ColorGrader g;
  BoundedHistory h(2);  // 最多保留 2 步撤销

  h.backup(g.save("v0")); g.set(10, 105);
  h.backup(g.save("v1")); g.set(20, 110);
  h.backup(g.save("v2")); g.set(30, 115);  // v0 被淘汰
  std::printf("压入 3 次（上限 2）：undoSize=%zu\n", h.undoSize());

  h.undo(g); g.dump("撤销 1 次");
  h.undo(g); g.dump("撤销 2 次");
  std::printf("尝试第 3 次撤销：%s\n", h.undo(g) ? "成功" : "已到栈底");

  h.redo(g); g.dump("重做 1 次");
  h.backup(g.save("分支")); g.set(99, 150);
  std::printf("新编辑后 redoSize=%zu\n", h.redoSize());
}
