// 《模板方法模式》：构造函数经过一个普通函数间接调到纯虚函数，编译器没有警告，运行时程序直接终止
#include <cstdio>

class ExportJob {
public:
    ExportJob() { log("创建"); }                 // 看上去只调了一个普通成员函数
    virtual ~ExportJob() = default;

protected:
    virtual const char* name() const = 0;

private:
    void log(const char* what) { std::printf("%s任务：%s\n", what, name()); }   // 这里调到了纯虚函数
};

class GifExport : public ExportJob {
protected:
    const char* name() const override { return "导出 GIF"; }
};

int main() {
    std::printf("开始\n");
    std::fflush(stdout);
    GifExport job;
    std::printf("这一行不会执行\n");
}
