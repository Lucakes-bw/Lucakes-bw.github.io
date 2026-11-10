// 《桥接模式》：SDL 的渲染 API 本身就是一座桥。同一段绘制代码，依次跑在每一个渲染驱动上
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <vector>

constexpr int VW = 1280, VH = 720;                   // 视频帧：720p，和专栏的测试视频一样大

// 只依赖 SDL_Renderer 这个「抽象」的代码：上传一帧 YUV、缩放画到窗口、画进度条。它不知道背后是哪个驱动
void drawFrame(SDL_Renderer* r, SDL_Texture* tex, const std::vector<Uint8>& yuv, double progress) {
    SDL_UpdateTexture(tex, nullptr, yuv.data(), VW);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_FRect dst{0, 0, 640, 360};
    SDL_RenderTexture(r, tex, nullptr, &dst);
    SDL_SetRenderDrawColor(r, 255, 128, 0, 255);
    SDL_FRect bar{0, 350, float(640 * progress), 10};
    SDL_RenderFillRect(r, &bar);
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("SDL_Init 失败：%s\n", SDL_GetError()); return 1; }
    std::vector<Uint8> yuv(VW * VH * 3 / 2);           // 一帧 I420：Y 平面是渐变，U、V 是灰
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) yuv[y * VW + x] = Uint8((x + y) / 8);
    std::fill(yuv.begin() + VW * VH, yuv.end(), 128);

    std::printf("视频驱动 %s，可用的渲染驱动 %d 个\n", SDL_GetCurrentVideoDriver(), SDL_GetNumRenderDrivers());
    SDL_Texture* first = nullptr;                    // 留着第一个驱动创建的纹理，看看别的驱动能不能用
    SDL_Renderer* firstR = nullptr;
    for (int d = 0; d < SDL_GetNumRenderDrivers(); ++d) {
        const char* name = SDL_GetRenderDriver(d);
        SDL_Window* win = SDL_CreateWindow("bridge", 640, 360, SDL_WINDOW_HIDDEN);
        SDL_Renderer* r = SDL_CreateRenderer(win, name);
        if (!r) { std::printf("  %-10s 创建失败：%s\n", name, SDL_GetError()); SDL_DestroyWindow(win); continue; }
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, VW, VH);

        drawFrame(r, tex, yuv, 0.35);                // 播放到 35%
        SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr);   // 显示之前读回画面，确认各个驱动画出来的一样
        Uint8 pr = 0, pg = 0, pb = 0, br = 0, bg = 0, bb = 0;
        if (shot) {
            SDL_ReadSurfacePixel(shot, 320, 180, &pr, &pg, &pb, nullptr);
            SDL_ReadSurfacePixel(shot, 10, 355, &br, &bg, &bb, nullptr);
            SDL_DestroySurface(shot);
        }
        SDL_RenderPresent(r);
        std::printf("  %-10s 画面中心 (%d,%d,%d)，进度条 (%d,%d,%d)\n", SDL_GetRendererName(r), pr, pg, pb, br, bg, bb);

        if (!first) { first = tex; firstR = r; continue; }      // 第一个驱动的渲染器和纹理先不释放
        if (!SDL_RenderTexture(r, first, nullptr, nullptr))
            std::printf("  %-10s 用 %s 创建的纹理：%s\n", "", SDL_GetRendererName(firstR), SDL_GetError());
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
    }
    if (first) { SDL_DestroyTexture(first); SDL_DestroyRenderer(firstR); }
    SDL_Quit();
}
