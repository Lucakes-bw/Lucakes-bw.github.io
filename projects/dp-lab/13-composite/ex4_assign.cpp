// 《组合》练习 4：给 Folder 补上拷贝赋值和移动操作（拷贝并交换）
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    const std::string& name() const { return name_; }
    virtual long long size() const = 0;
    virtual std::unique_ptr<Component> clone() const = 0;
protected:
    Component(const Component&) = default;
    Component& operator=(const Component&) = default;
    Component(Component&&) = default;
    Component& operator=(Component&&) = default;
private:
    std::string name_;
};

class File : public Component {
public:
    File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
    std::unique_ptr<Component> clone() const override { return std::make_unique<File>(*this); }
private:
    long long bytes_;
};

class Folder : public Component {
public:
    using Component::Component;
    Folder(const Folder& other) : Component(other) {
        for (const auto& c : other.children_) children_.push_back(c->clone());
    }
    Folder(Folder&&) noexcept = default;              // 移动：直接接管子节点，不复制
    Folder& operator=(Folder other) noexcept {         // 拷贝并交换：按值传参时已经复制（或移动）好了
        Component::operator=(std::move(other));        // 名字等基类的成员
        children_.swap(other.children_);               // 换过来；原来的子节点随 other 一起销毁
        return *this;
    }
    std::unique_ptr<Component> clone() const override { return std::make_unique<Folder>(*this); }

    template <class T, class... Args>
    T& add(Args&&... args) {
        children_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return static_cast<T&>(*children_.back());
    }
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

int main() {
    Folder a("a"), b("b");
    a.add<File>("x.txt", 1000);
    b.add<Folder>("sub").add<File>("y.txt", 2000);

    a = b;                                             // 拷贝赋值：a 变成 b 的一份深拷贝
    b.add<File>("z.txt", 4000);                        // 再改 b，a 不受影响
    std::cout << a.name() << " " << a.size() << "，" << b.name() << " " << b.size() << '\n';

    Folder& same = a;
    a = same;                                          // 自赋值也安全
    std::cout << "自赋值之后 " << a.size() << '\n';

    Folder c = std::move(b);                           // 移动：不复制子节点
    std::cout << c.name() << " " << c.size() << '\n';
}
