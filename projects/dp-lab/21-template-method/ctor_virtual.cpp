// 《模板方法模式》：在构造函数和析构函数里调虚函数——调到的是基类自己的版本，不是子类的
#include <cstdio>
#include <string>

class ExportJob {
public:
    ExportJob() { std::printf("创建任务：%s\n", name()); }               // 这时子类部分还没构造
    virtual ~ExportJob() {
        std::printf("销毁任务：%s\n", name());                           // 这时子类部分已经析构了
        removeTemp();
    }
    virtual const char* name() const { return "（未命名任务）"; }

protected:
    virtual void removeTemp() { std::printf("  ExportJob::removeTemp：没有临时文件\n"); }
};

class GifExport : public ExportJob {
public:
    GifExport() : palette_("palette.tmp") { std::printf("  GifExport 构造完，调色板 %s\n", palette_.c_str()); }
    ~GifExport() override { std::printf("  GifExport 析构\n"); }
    const char* name() const override { return "导出 GIF"; }

protected:
    void removeTemp() override { std::printf("  删除 %s\n", palette_.c_str()); }

private:
    std::string palette_;
};

int main() {
    GifExport job;
    std::printf("main 里调用 job.name()：%s\n", job.name());
}
