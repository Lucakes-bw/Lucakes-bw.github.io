// 《RAII 与 Scope Guard》：问题版——手动释放 C 风格解码管线句柄，多条提前 return 路径导致资源泄漏
#include <cstdio>

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

// failStep: 0=正常, 2=打开解码器后初始化失败, 4=解码帧失败
int decodeFirstFrame(const char* url, int failStep) {
  Demuxer* dm = open_demuxer(url);
  if (!dm) return -1;

  Decoder* dec = open_decoder(dm);
  if (!dec || failStep == 2) {
    // 坑 ①：初始化失败时只释放了 dec，忘了 close_demuxer(dm)！
    close_decoder(dec);
    return -2;
  }

  FrameBuf* fb = alloc_frame(1920 * 1080);
  if (!fb) {
    close_decoder(dec);
    close_demuxer(dm);
    return -3;
  }

  if (failStep == 4) {
    // 坑 ②：新增提前返回分支，直接 return -4，三个句柄全部泄漏！
    return -4;
  }

  free_frame(fb);
  close_decoder(dec);
  close_demuxer(dm);
  return 0;
}

int main() {
  std::printf("== 场景 1：正常路径 ==\n");
  int rc1 = decodeFirstFrame("clip.mp4", 0);
  std::printf("rc=%d, 未释放句柄=%d\n", rc1, g_live);

  std::printf("== 场景 2：解码器初始化失败 ==\n");
  int rc2 = decodeFirstFrame("clip.mp4", 2);
  std::printf("rc=%d, 未释放句柄=%d\n", rc2, g_live);

  std::printf("== 场景 3：解码帧失败 ==\n");
  int rc3 = decodeFirstFrame("clip.mp4", 4);
  std::printf("rc=%d, 未释放句柄=%d\n", rc3, g_live);
}
