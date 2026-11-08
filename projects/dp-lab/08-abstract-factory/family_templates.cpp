// 《抽象工厂》：编译期的产品族——每个族是一个类型，族里的产品和帧都是它的成员类型
#include <iostream>

// ---------- 软件族：帧在内存里 ----------
struct SoftwareFamily {
    struct Frame { int index; const unsigned char* pixels; };
    struct Decoder { Frame decode(int i) { return {i, nullptr}; } };
    struct Renderer { void draw(const Frame& f) { std::cout << "  SDL 画第 " << f.index << " 帧（内存）\n"; } };
};

// ---------- VideoToolbox 族：帧在 GPU 上 ----------
struct VideoToolboxFamily {
    struct Frame { int index; void* pixelBuffer; };    // 真实代码里是 CVPixelBufferRef
    struct Decoder { Frame decode(int i) { return {i, nullptr}; } };
    struct Renderer { void draw(const Frame& f) { std::cout << "  Metal 画第 " << f.index << " 帧（GPU）\n"; } };
};

// 播放器按「族」实例化：解码器和渲染器只能来自同一个族
template <typename Family>
void play(int frames) {
    typename Family::Decoder decoder;
    typename Family::Renderer renderer;
    for (int i = 1; i <= frames; ++i) renderer.draw(decoder.decode(i));
}

int main() {
    std::cout << "play<SoftwareFamily>\n";
    play<SoftwareFamily>(2);
    std::cout << "play<VideoToolboxFamily>\n";
    play<VideoToolboxFamily>(2);
}
