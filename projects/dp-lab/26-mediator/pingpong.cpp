// 《中介者模式》：中介者更新同事时，同事又通知中介者——用 command line args 切换：默认带重入保护，传 nbl 复现无限递归
#include <cmath>
#include <cstdio>
#include <cstring>

struct Slider;
struct SpinBox;
struct TimebarMediator {
  virtual ~TimebarMediator() = default;
  virtual void onChanged(void* sender) = 0;
};

struct Slider {                       // 进度条滑块：整数帧号 [0, 240]
  TimebarMediator* med = nullptr;
  int frame = 0;
  int updates = 0;
  void setFrame(int f) {
    frame = f;
    ++updates;
    if (med) med->onChanged(this);    // 只要被调用就通知中介者（Qt 的 setValue 遇到微小差值时就是这样）
  }
};

struct SpinBox {                      // 时间输入框：秒数（保留 2 位小数，写回时有舍入误差）
  TimebarMediator* med = nullptr;
  double sec = 0.0;
  int updates = 0;
  void setSec(double s) {
    sec = std::round(s * 100.0) / 100.0;  // 输入框只保留两位小数：1 / 24.0 = 0.041666... → 0.04
    ++updates;
    if (med) med->onChanged(this);
  }
};

// 写法 1：没有重入保护。滑块改到第 1 帧 → 输入框算得 0.04 秒 → 反算回帧号 int(0.04 * 24) = 0 ≠ 1 → 滑块又改回 0……
struct NaiveBar : TimebarMediator {
  Slider slider{this};
  SpinBox spin{this};
  int depth = 0, maxDepth = 0;
  bool limit;
  explicit NaiveBar(bool lim) : limit(lim) {}

  void onChanged(void* sender) override {
    if (limit && depth >= 6) return;  // 演示模式：到 6 层停下，看它是怎么来回乒乓的
    ++depth;
    if (depth > maxDepth) maxDepth = depth;
    if (sender == &slider) {
      spin.setSec(slider.frame / 24.0);
    } else if (sender == &spin) {
      slider.setFrame(static_cast<int>(spin.sec * 24.0));
    }
    --depth;
  }
};

// 写法 2：中介者用 syncing_ 守卫（类似 Qt 的 QSignalBlocker）——正在同步引起的 set 不再二次触发联动
struct SafeBar : TimebarMediator {
  Slider slider{this};
  SpinBox spin{this};
  bool syncing_ = false;

  void onChanged(void* sender) override {
    if (syncing_) return;
    syncing_ = true;
    if (sender == &slider) {
      spin.setSec(slider.frame / 24.0);
    } else if (sender == &spin) {
      slider.setFrame(static_cast<int>(spin.sec * 24.0));
    }
    syncing_ = false;
  }
};

int main(int argc, char** argv) {
  bool crash = (argc > 1 && std::strcmp(argv[1], "nbl") == 0);
  if (crash) {
    NaiveBar b(false);
    b.slider.setFrame(1);             // 无限递归直到栈溢出
    return 0;
  }
  NaiveBar b1(true);
  b1.slider.setFrame(1);
  std::printf("无重入保护（拦在第 6 层）：拖到第 1 帧后，slider.frame=%d spin.sec=%.2f，"
              "slider 被改 %d 次、spin 被改 %d 次\n",
              b1.slider.frame, b1.spin.sec, b1.slider.updates, b1.spin.updates);

  SafeBar b2;
  b2.slider.setFrame(1);
  std::printf("带 syncing_ 保护        ：拖到第 1 帧后，slider.frame=%d spin.sec=%.2f，"
              "slider 被改 %d 次、spin 被改 %d 次\n",
              b2.slider.frame, b2.spin.sec, b2.slider.updates, b2.spin.updates);
}
