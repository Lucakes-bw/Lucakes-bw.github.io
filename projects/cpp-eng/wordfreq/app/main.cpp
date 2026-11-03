// wordfreq：统计文本中出现次数最多的单词
//   wordfreq [-n 个数] [-j 线程数] [文件 ...]    没有给出文件时读取标准输入
#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fmt/core.h>
#include <CLI/CLI.hpp>

#include "wordfreq/counter.h"

namespace {

bool readAll(std::istream& in, std::string& out)
{
    std::ostringstream ss;
    ss << in.rdbuf();
    out += ss.str();
    return !in.bad();
}

int run(int argc, char** argv)
{
    CLI::App app{"统计文本中出现次数最多的单词"};
    std::size_t topN = 10;
    unsigned threads = 1;
    std::vector<std::string> files;
    app.add_option("-n,--top", topN, "列出前几个单词")->capture_default_str()->check(CLI::Range(1, 100000));
    app.add_option("-j,--threads", threads, "统计用的线程数，0 表示和 CPU 核数相同")->capture_default_str();
    app.add_option("files", files, "要统计的文件，不给出时读取标准输入")->check(CLI::ExistingFile);
    CLI11_PARSE(app, argc, argv);

    std::string text;
    if (files.empty()) {
        readAll(std::cin, text);
    } else {
        for (const auto& path : files) {
            std::ifstream in(path, std::ios::binary);
            if (!in || !readAll(in, text)) {
                fmt::print(stderr, "无法读取文件：{}\n", path);
                return 1;
            }
            text += '\n';  // 两个文件之间加一个分隔符
        }
    }

    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    wordfreq::Counter counter = wordfreq::countParallel(text, threads);

    fmt::print("单词 {} 个，不同的 {} 个\n", counter.total(), counter.unique());
    std::size_t rank = 0;
    for (const auto& e : counter.top(topN)) {
        double percent = 100.0 * static_cast<double>(e.count) / static_cast<double>(counter.total());
        fmt::print("{:>4}  {:<14}{:>9}  {:>6.2f}%\n", ++rank, e.word, e.count, percent);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    // 异常不能逃出 main：那样程序会直接调用 std::terminate，不会输出任何有用的信息
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        fmt::print(stderr, "错误：{}\n", e.what());
    } catch (...) {
        fmt::print(stderr, "错误：未知的异常\n");
    }
    return 1;
}
