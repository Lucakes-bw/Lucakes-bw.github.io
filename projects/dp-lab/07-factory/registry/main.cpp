// 《工厂方法》：按文件内容识别格式，交给对应的解析器
#include <cstdio>
#include <fstream>
#include <iterator>
#include "parser.h"

int main(int argc, char** argv) {
    std::printf("已注册的格式：");
    for (const auto& f : Registry::instance().formats()) std::printf("%s ", f.name.c_str());
    std::printf("（共 %zu 种）\n", Registry::instance().formats().size());

    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const Format* f = Registry::instance().best(data);
        std::printf("%s（%zu 字节）：%s\n", argv[i], data.size(), f ? f->name.c_str() : "无法识别");
        if (f) f->create()->parse(data);             // 工厂：调用者只拿到 Parser 接口，不知道具体是哪个类
    }
}
