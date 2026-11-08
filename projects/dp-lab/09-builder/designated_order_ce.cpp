// 《建造者》：C++20 指定初始化必须按成员声明的顺序写（故意编译不过）
#include <string>

struct EncodeOptions {
    std::string input;
    std::string output;
    int crf = 23;
    int width = 0, height = 0;
};

int main() {
    EncodeOptions o{.width = 640, .height = 360, .input = "bbb-10s.mp4"};   // 顺序和声明不一致
    (void)o;
}
