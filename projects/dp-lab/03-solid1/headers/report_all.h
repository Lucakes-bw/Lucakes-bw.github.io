// 一个什么都管的报表类：解析、校验、统计、格式化、保存都在这里（头文件里直接实现）
#pragma once
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

class ReportService {
public:
    // 解析：一行一个数字
    std::vector<double> parse(const std::string& text) const {
        std::vector<double> v;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) v.push_back(std::stod(line));
        return v;
    }
    // 校验：每一行必须是数字
    bool validate(const std::string& line) const {
        static const std::regex number(R"(^-?\d+(\.\d+)?$)");
        return std::regex_match(line, number);
    }
    // 统计
    double average(const std::vector<double>& v) const {
        return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }
    // 格式化
    std::string toJson(const std::map<std::string, double>& m) const {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << '{';
        for (auto it = m.begin(); it != m.end(); ++it) out << (it == m.begin() ? "" : ", ") << '"' << it->first << "\": " << it->second;
        return out.str() + '}';
    }
    // 保存
    void save(const std::string& path, const std::string& content) const {
        std::ofstream(path) << content;
    }
};
