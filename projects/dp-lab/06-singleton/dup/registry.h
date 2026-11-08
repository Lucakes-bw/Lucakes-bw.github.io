// 《单例》：放在头文件里的 Meyers 单例，被两个动态库各自包含
#pragma once

class Registry {
public:
    static Registry& instance() {      // 类内定义的函数是 inline 的，每个包含它的模块里都有一份
        static Registry r;
        return r;
    }
    int count = 0;
};

#define API __attribute__((visibility("default")))
