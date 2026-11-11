// 《模板方法模式》：构造函数里直接调纯虚函数——编译器给出警告，链接时找不到函数体
#include <cstdio>

class ExportJob {
public:
    ExportJob() { std::printf("创建任务：%s\n", name()); }
    virtual ~ExportJob() = default;

protected:
    virtual const char* name() const = 0;
};

class GifExport : public ExportJob {
protected:
    const char* name() const override { return "导出 GIF"; }
};

int main() { GifExport job; }
