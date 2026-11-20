// 《备忘录模式》练习 3 参考：修复「可变 shared_ptr 浅拷贝污染历史快照」的 bug（改为 shared_ptr<const> + COW）
#include <cstdio>
#include <memory>
#include <vector>

struct Curve {
  std::vector<int> pts;  // 色调曲线控制点
};

class CurveEditor {
 public:
  class Snapshot {
    friend class CurveEditor;
    std::shared_ptr<const Curve> curve_;
    explicit Snapshot(std::shared_ptr<const Curve> c) : curve_(std::move(c)) {}
  };

  CurveEditor() : curve_(std::make_shared<const Curve>(Curve{{0, 128, 255}})) {}

  // 关键修复：写时复制（COW）——若曲线被历史快照共享，先拷贝一份新的再修改
  void setMidpoint(int v) {
    auto next = std::make_shared<Curve>(*curve_);
    next->pts[1] = v;
    curve_ = std::move(next);
  }
  Snapshot save() const { return Snapshot(curve_); }
  void restore(const Snapshot& s) { curve_ = s.curve_; }
  int midpoint() const { return curve_->pts[1]; }

 private:
  std::shared_ptr<const Curve> curve_;
};

int main() {
  CurveEditor ed;
  auto s0 = ed.save();
  ed.setMidpoint(180);
  std::printf("修改后 midpoint=%d\n", ed.midpoint());
  ed.restore(s0);
  std::printf("恢复后 midpoint=%d（未被污染）\n", ed.midpoint());
}
