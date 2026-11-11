// 《责任链模式》练习 3：解码出错时的处理链——重试 → 跳到下一个关键帧 → 换成软解 → 放弃
#include <cstdio>
#include <string>

enum class Err { Again, Corrupt, HwLost, NoMemory };
struct DecodeError { Err kind; int pts; int retries = 0; bool onHw = false; };

class ErrorHandler {
 public:
  virtual ~ErrorHandler() = default;
  ErrorHandler* setNext(ErrorHandler* next) { next_ = next; return next; }
  std::string handle(DecodeError& e) {       // 模板方法：子类只回答「能不能处理」，转发由基类负责，不会忘
    if (std::string r = tryHandle(e); !r.empty()) return r;
    return next_ ? next_->handle(e) : "放弃：停止播放并报错";
  }
 protected:
  virtual std::string tryHandle(DecodeError& e) = 0;
 private:
  ErrorHandler* next_ = nullptr;
};

class Retry : public ErrorHandler {         // 暂时性错误：最多重试 2 次
  std::string tryHandle(DecodeError& e) override {
    if (e.kind != Err::Again || e.retries >= 2) return "";
    return "重试第 " + std::to_string(++e.retries) + " 次";
  }
};
class SkipToKeyframe : public ErrorHandler {   // 数据损坏：丢掉到下一个关键帧之前的包
  std::string tryHandle(DecodeError& e) override {
    if (e.kind != Err::Corrupt) return "";
    return "跳到 pts " + std::to_string((e.pts / 48 + 1) * 48) + " 的关键帧";   // 假设每 48 帧一个关键帧
  }
};
class FallbackToSoftware : public ErrorHandler {   // 硬件设备丢了（比如显卡驱动重置）：换软解
  std::string tryHandle(DecodeError& e) override {
    if (e.kind != Err::HwLost || !e.onHw) return "";
    e.onHw = false;
    return "关掉硬解，换成软解从 pts " + std::to_string(e.pts) + " 继续";
  }
};

int main() {
  Retry retry; SkipToKeyframe skip; FallbackToSoftware fallback;
  retry.setNext(&skip)->setNext(&fallback);
  DecodeError again{Err::Again, 10};
  for (int i = 0; i < 3; ++i) std::printf("EAGAIN  pts 10：%s\n", retry.handle(again).c_str());
  DecodeError corrupt{Err::Corrupt, 61};
  std::printf("损坏    pts 61：%s\n", retry.handle(corrupt).c_str());
  DecodeError lost{Err::HwLost, 130, 0, true};
  std::printf("设备丢失 pts 130：%s\n", retry.handle(lost).c_str());
  DecodeError oom{Err::NoMemory, 200};
  std::printf("内存不足 pts 200：%s\n", retry.handle(oom).c_str());
}
