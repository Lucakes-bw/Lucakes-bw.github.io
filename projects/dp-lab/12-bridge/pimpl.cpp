// 《桥接模式》：Pimpl 是只有一种实现的桥接（为了放在一个文件里，头文件和实现文件用注释分开）
#include <cstdio>
#include <memory>
#include <string>

// ===== player.h：使用者只能看到这些 =====
class Player {
public:
    Player();
    ~Player();                                       // 必须在 Impl 完整的地方定义（见下面）
    void open(const std::string& path);
    void play();
private:
    struct Impl;                                     // 只声明，不定义：实现细节一个都不出现在头文件里
    std::unique_ptr<Impl> impl_;                     // 桥：抽象这边只有一个指针
};

// ===== player.cpp：改这里的成员，包含 player.h 的文件都不用重新编译 =====
struct Player::Impl {
    std::string path;
    int decodedFrames = 0;
    // 真实的播放器这里还有 FFmpeg 的上下文、解码线程、音频设备……
    void decodeSome() { decodedFrames += 24; }
};

Player::Player() : impl_(std::make_unique<Impl>()) {}
Player::~Player() = default;                         // 这里 Impl 已经完整，unique_ptr 知道怎么删除它
void Player::open(const std::string& path) { impl_->path = path; }
void Player::play() {
    impl_->decodeSome();
    std::printf("播放 %s，已解码 %d 帧\n", impl_->path.c_str(), impl_->decodedFrames);
}

// ===== main.cpp =====
int main() {
    Player p;
    p.open("bbb-10s.mp4");
    p.play();
    std::printf("sizeof(Player) = %zu（只有一个指针）\n", sizeof(Player));
}
