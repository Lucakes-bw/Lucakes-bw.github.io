// 《建造者》：类型状态建造者——没设置输入和输出，build() 根本不存在
#include <iostream>
#include <string>

struct EncodeJob {
    std::string input, output;
    int crf = 23;
    std::string commandLine() const { return "ffmpeg -i " + input + " -c:v libx264 -crf " + std::to_string(crf) + " " + output; }
};

// 两个模板参数记录「输入设置了没有」「输出设置了没有」，每设置一项，返回的就是另一个类型
template <bool HasInput, bool HasOutput>
class JobBuilder {
public:
    JobBuilder() = default;
    explicit JobBuilder(EncodeJob j) : job_(std::move(j)) {}

    JobBuilder<true, HasOutput> input(std::string p) && { job_.input = std::move(p); return JobBuilder<true, HasOutput>(std::move(job_)); }
    JobBuilder<HasInput, true> output(std::string p) && { job_.output = std::move(p); return JobBuilder<HasInput, true>(std::move(job_)); }
    JobBuilder crf(int q) && { job_.crf = q; return std::move(*this); }

    EncodeJob build() && {
        static_assert(HasInput, "必须先调用 input()");
        static_assert(HasOutput, "必须先调用 output()");
        return std::move(job_);
    }

private:
    EncodeJob job_;
};

using EncodeJobBuilder = JobBuilder<false, false>;

int main() {
    EncodeJob job = EncodeJobBuilder().input("bbb-10s.mp4").crf(28).output("preview.mp4").build();
    std::cout << job.commandLine() << '\n';
    // EncodeJobBuilder().input("bbb-10s.mp4").build();   // 编译错误：必须先调用 output()
}
