// 生成测试用的文本：gentext <字节数> > 文件
#include <cstdio>
#include <cstdlib>

#include "text_gen.h"

int main(int argc, char** argv)
{
    std::size_t bytes = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    std::string text = generateText(bytes);
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}
