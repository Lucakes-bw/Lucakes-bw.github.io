// 《RAII 与 Scope Guard》：标准 RAII 封装——unique_ptr 空删除器（Rule of 0）与手写只移动 Handle（Rule of 5）
#include <cstdio>
#include <memory>
#include <utility>

struct Demuxer  { int id; };
struct Decoder  { int id; };
struct FrameBuf { int id; };

static int g_live = 0;

Demuxer*  open_demuxer(const char*)  { ++g_live; std::printf("  [+] open  Demuxer\n");  return new Demuxer{1}; }
void      close_demuxer(Demuxer* p)  { if (p) { --g_live; std::printf("  [-] close Demuxer\n");  delete p; } }
Decoder*  open_decoder(Demuxer*)     { ++g_live; std::printf("  [+] open  Decoder\n");  return new Decoder{2}; }
void      close_decoder(Decoder* p)  { if (p) { --g_live; std::printf("  [-] close Decoder\n");  delete p; } }
FrameBuf* alloc_frame(int)           { ++g_live; std::printf("  [+] alloc FrameBuf\n"); return new FrameBuf{3}; }
void      free_frame(FrameBuf* p)    { if (p) { --g_live; std::printf("  [-] free  FrameBuf\n"); delete p; } }

// 方式 A：空函数对象删除器 + std::unique_ptr（零额外空间开销，推荐用于 C 指针句柄）
struct DemuxerDeleter { void operator()(Demuxer* p) const noexcept { close_demuxer(p); } };
struct DecoderDeleter { void operator()(Decoder* p) const noexcept { close_decoder(p); } };
using DemuxerPtr = std::unique_ptr<Demuxer, DemuxerDeleter>;
using DecoderPtr = std::unique_ptr<Decoder, DecoderDeleter>;

// 方式 B：手写只移动 RAII Handle（Rule of 5：禁拷贝、支持移动、析构自动释放）
class FrameHandle {
 public:
  explicit FrameHandle(FrameBuf* p = nullptr) noexcept : ptr_(p) {}
  ~FrameHandle() { free_frame(ptr_); }
  FrameHandle(const FrameHandle&) = delete;
  FrameHandle& operator=(const FrameHandle&) = delete;
  FrameHandle(FrameHandle&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
  FrameHandle& operator=(FrameHandle&& o) noexcept {
    if (this != &o) {
      free_frame(ptr_);
      ptr_ = std::exchange(o.ptr_, nullptr);
    }
    return *this;
  }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }
  FrameBuf* get() const noexcept { return ptr_; }
 private:
  FrameBuf* ptr_;
};

int decodeFirstFrame(const char* url, int failStep) {
  DemuxerPtr dm(open_demuxer(url));
  if (!dm) return -1;

  DecoderPtr dec(open_decoder(dm.get()));
  if (!dec || failStep == 2) return -2;      // 提前返回：dec → dm 自动逆序释放

  FrameHandle fb(alloc_frame(1920 * 1080));
  if (!fb) return -3;

  if (failStep == 4) return -4;              // 提前返回：fb → dec → dm 自动逆序释放
  return 0;                                  // 正常返回：fb → dec → dm 自动逆序释放
}

int main() {
  for (int step : {0, 2, 4}) {
    std::printf("== failStep=%d ==\n", step);
    int rc = decodeFirstFrame("clip.mp4", step);
    std::printf("rc=%d, 未释放句柄=%d\n", rc, g_live);
  }
}
