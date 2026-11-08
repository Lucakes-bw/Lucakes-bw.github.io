// 《抽象工厂》：编译期的产品族里，混用两个族的产品是编译错误（故意编译不过）
struct SoftwareFamily {
    struct Frame { int index; const unsigned char* pixels; };
    struct Decoder { Frame decode(int i) { return {i, nullptr}; } };
    struct Renderer { void draw(const Frame&) {} };
};
struct VideoToolboxFamily {
    struct Frame { int index; void* pixelBuffer; };
    struct Decoder { Frame decode(int i) { return {i, nullptr}; } };
    struct Renderer { void draw(const Frame&) {} };
};

int main() {
    VideoToolboxFamily::Decoder decoder;   // 硬解
    SoftwareFamily::Renderer renderer;     // 却用了软件的渲染器
    renderer.draw(decoder.decode(1));
}
