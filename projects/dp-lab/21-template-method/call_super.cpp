// 《模板方法模式》：「重写时记得调基类」的写法里，子类漏调一次，批量导出到第 N 个就打不开文件；NVI 写法漏不了
#include <cstdio>
#include <cstdlib>
#include <string>

int g_open = 3;                                  // 打开着的文件描述符：stdin、stdout、stderr 已经占了 3 个
int g_max_open = 256;                            // 模拟 macOS 的默认软限制（launchctl limit maxfiles）；Linux 一般是 1024

struct File {
    bool open(const std::string&) {
        if (g_open >= g_max_open) return false;    // Too many open files
        ++g_open;
        return true;
    }
    void close() { --g_open; }
};

namespace call_super {                           // 写法一：步骤是虚函数，子类重写时「必须先调基类的版本」
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {
        if (!prepare(in, out)) return false;
        /* ……读包、解码、编码、写文件…… */
        cleanup();
        return true;
    }

protected:
    virtual bool prepare(const std::string& in, const std::string& out) {   // 重写时必须调 ExportJob::prepare
        if (!input_.open(in)) return false;
        if (!output_.open(out)) { input_.close(); return false; }
        return true;
    }
    virtual void cleanup() {                                                // 重写时必须调 ExportJob::cleanup
        output_.close();
        input_.close();
    }

private:
    File input_, output_;
};

class GifExport : public ExportJob {             // GIF 先生成调色板，存在一个临时文件里
protected:
    bool prepare(const std::string& in, const std::string& out) override {
        if (!ExportJob::prepare(in, out)) return false;
        return palette_.open("palette.tmp");
    }
    void cleanup() override {
        palette_.close();                        // 忘了调 ExportJob::cleanup()：输入和输出都没关
    }

private:
    File palette_;
};
}  // namespace call_super

namespace nvi {                                  // 写法二：基类自己做完公共的步骤，只在中间留出钩子
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {
        if (!input_.open(in)) return false;
        if (!output_.open(out)) { input_.close(); return false; }
        bool ok = onPrepare();
        if (ok) { /* ……读包、解码、编码、写文件…… */ }
        onCleanup();                             // 先让子类清理自己的东西
        output_.close();                         // 公共的清理永远由基类来做
        input_.close();
        return ok;
    }

protected:
    virtual bool onPrepare() { return true; }    // 钩子：默认什么都不做
    virtual void onCleanup() {}

private:
    File input_, output_;
};

class GifExport : public ExportJob {
protected:
    bool onPrepare() override { return palette_.open("palette.tmp"); }
    void onCleanup() override { palette_.close(); }   // 不需要、也没法调基类的 cleanup

private:
    File palette_;
};
}  // namespace nvi

template <class Job>
void batch(const char* name, int count) {        // 批量导出 count 个 GIF
    g_open = 3;
    int ok = 0;
    for (int i = 1; i <= count; ++i) {
        Job job;
        if (!job.run("clip" + std::to_string(i) + ".mp4", "clip" + std::to_string(i) + ".gif")) {
            std::printf("%s：第 %d 个任务打不开文件（Too many open files）\n", name, i);
            break;
        }
        ++ok;
    }
    std::printf("%s：成功 %d 个，打开着的文件描述符 %d 个\n", name, ok, g_open);
}

int main(int argc, char** argv) {
    if (argc > 1) g_max_open = std::atoi(argv[1]);
    batch<call_super::GifExport>("调基类的写法", 1000);
    batch<nvi::GifExport>("NVI 写法    ", 1000);
}
