// 《组合》：叶子和容器分开处理——每个函数都要判断节点是文件还是文件夹，加了压缩包之后算错了大小
#include <iostream>
#include <memory>
#include <string>
#include <vector>

enum class Kind { File, Folder, Archive };   // Archive（压缩包）是后来加的

struct Node {
    Kind kind;
    std::string name;
    long long bytes = 0;                          // 文件、压缩包自己的大小；文件夹不用
    std::vector<std::unique_ptr<Node>> children;  // 文件夹、压缩包里的内容；文件不用
};

Node* add(Node* parent, Kind kind, std::string name, long long bytes = 0) {
    parent->children.push_back(std::make_unique<Node>(Node{kind, std::move(name), bytes, {}}));
    return parent->children.back().get();
}

// 每个函数都要自己判断节点的种类、自己递归
long long totalSize(const Node* n) {
    if (n->kind == Kind::File) return n->bytes;
    long long sum = 0;                            // 加 Archive 时漏改了这里：压缩包走进了「文件夹」的分支
    for (const auto& c : n->children) sum += totalSize(c.get());
    return sum;
}

int countFiles(const Node* n) {
    if (n->kind == Kind::File || n->kind == Kind::Archive) return 1;   // 这里记得改了：压缩包算一个文件
    int count = 0;
    for (const auto& c : n->children) count += countFiles(c.get());
    return count;
}

void print(const Node* n, int depth = 0) {
    std::cout << std::string(depth * 2, ' ') << n->name;
    if (n->kind == Kind::Folder) std::cout << "/";
    else if (n->kind == Kind::Archive) std::cout << "（压缩包，" << n->bytes / 1024 << " KB）";   // 这里也改了
    else std::cout << "  " << n->bytes / 1024 << " KB";
    std::cout << '\n';
    for (const auto& c : n->children) print(c.get(), depth + 1);
}

int main() {
    Node root{Kind::Folder, "project", 0, {}};
    add(&root, Kind::File, "README.md", 2048);
    Node* src = add(&root, Kind::Folder, "src");
    add(src, Kind::File, "main.cpp", 12288);
    add(src, Kind::File, "player.cpp", 30720);
    Node* assets = add(&root, Kind::Folder, "assets");
    add(assets, Kind::File, "logo.png", 49152);
    Node* zip = add(assets, Kind::Archive, "logs.zip", 1048576);         // 压缩后 1 MB
    add(zip, Kind::File, "decode.log", 6291456);                          // 解压后 6 MB + 4 MB
    add(zip, Kind::File, "encode.log", 4194304);

    print(&root);
    std::cout << countFiles(&root) << " 个文件，共 " << totalSize(&root) / 1024 << " KB\n";
}
