// 《模板方法模式》：标准库里的 NVI——ostream 调 streambuf 的公有非虚函数 sputc，缓冲区满了才调受保护的虚函数 overflow
#include <cstdio>
#include <ostream>
#include <streambuf>

// 把字幕文本写到 stdout，顺便统计字节数和行数；只重写两个受保护的虚函数 overflow 和 sync
class CountingBuf : public std::streambuf {
public:
    long bytes = 0, lines = 0, calls = 0;
    CountingBuf(int size, bool echo) : size_(size), echo_(echo) { setp(buf_, buf_ + size_); }   // size = 0：没有缓冲区
    ~CountingBuf() override { sync(); }

protected:
    int_type overflow(int_type ch) override {    // 基类的 sputc() 发现缓冲区放不下时才调用它
        ++calls;
        drain();
        if (!traits_type::eq_int_type(ch, traits_type::eof())) emit(static_cast<char>(ch));
        return traits_type::not_eof(ch);
    }
    int sync() override { drain(); return 0; }   // flush 时调用

private:
    char buf_[64];
    int size_;
    bool echo_;
    void emit(char c) { ++bytes; if (c == '\n') ++lines; if (echo_) std::putchar(c); }
    void drain() {                               // 把缓冲区里攒下的字符写出去
        for (char* p = pbase(); p < pptr(); ++p) emit(*p);
        setp(buf_, buf_ + size_);
    }
};

void writeSubs(std::ostream& out) {              // 只用 ostream 的公有接口
    out << "1\n00:00:01,000 --> 00:00:03,500\n大雄兔醒了。\n\n";
    out << "2\n00:00:04,000 --> 00:00:06,000\n" << "第 " << 2 << " 条字幕\n";
    out.flush();
}

int main() {
    for (int size : {0, 16, 64}) {
        CountingBuf buf(size, size == 0);            // 只在第一次把字幕打印出来
        std::ostream out(&buf);
        writeSubs(out);
        std::printf("-- 缓冲区 %2d 字节：写了 %ld 字节、%ld 行，overflow 被调用 %ld 次\n",
                    size, buf.bytes, buf.lines, buf.calls);
    }
}
