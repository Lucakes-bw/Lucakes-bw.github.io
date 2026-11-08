// 《建造者》：忘了设置输出，类型状态建造者在编译期就报错（故意编译不过）
#include <string>

struct EncodeJob { std::string input, output; };

template <bool HasInput, bool HasOutput>
class JobBuilder {
public:
    JobBuilder() = default;
    explicit JobBuilder(EncodeJob j) : job_(std::move(j)) {}
    JobBuilder<true, HasOutput> input(std::string p) && { job_.input = std::move(p); return JobBuilder<true, HasOutput>(std::move(job_)); }
    JobBuilder<HasInput, true> output(std::string p) && { job_.output = std::move(p); return JobBuilder<HasInput, true>(std::move(job_)); }
    EncodeJob build() && {
        static_assert(HasInput, "必须先调用 input()");
        static_assert(HasOutput, "必须先调用 output()");
        return std::move(job_);
    }
private:
    EncodeJob job_;
};

int main() {
    EncodeJob job = JobBuilder<false, false>().input("bbb-10s.mp4").build();   // 忘了 output
    (void)job;
}
