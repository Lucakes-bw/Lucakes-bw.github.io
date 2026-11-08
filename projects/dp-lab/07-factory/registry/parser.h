// 《工厂方法》：解析器接口和注册表
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

class Parser {
public:
    virtual ~Parser() = default;
    virtual void parse(const Bytes& data) = 0;       // 解析整个文件，打印摘要
};

// 注册表：每种格式登记三样东西——名字、探测函数（看文件开头的字节，给出 0～100 的把握）、创建函数
struct Format {
    std::string name;
    std::function<int(const Bytes&)> probe;
    std::function<std::unique_ptr<Parser>()> create;
};

class Registry {
public:
    static Registry& instance();                     // Meyers 单例：保证在任何注册之前就已经构造好
    void add(Format f) { formats_.push_back(std::move(f)); }
    const std::vector<Format>& formats() const { return formats_; }
    const Format* best(const Bytes& data) const;     // 探测得分最高的格式

private:
    std::vector<Format> formats_;
};

// 每个格式的源文件里定义一个 Registrar 静态对象，它的构造函数在 main 之前运行，完成注册
struct Registrar {
    explicit Registrar(Format f) { Registry::instance().add(std::move(f)); }
};

inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
