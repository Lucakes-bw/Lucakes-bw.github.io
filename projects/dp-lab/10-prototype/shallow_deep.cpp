// 《原型》：浅拷贝和深拷贝——从模板新建文档，改了新文档，模板也跟着变了
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Layer {
    std::string name;
    std::string color;
};

struct Document {
    std::string title;
    std::vector<std::shared_ptr<Layer>> layers;    // 图层用 shared_ptr 保存

    Document shallowCopy() const { return *this; }  // 默认的拷贝：只复制了指针，图层还是同一份

    Document deepCopy() const {                      // 深拷贝：每个图层都复制一份新的
        Document d{title, {}};
        for (const auto& l : layers) d.layers.push_back(std::make_shared<Layer>(*l));
        return d;
    }
};

void show(const char* who, const Document& d) {
    std::cout << "  " << who << "《" << d.title << "》的背景：" << d.layers[0]->color
              << "（这个图层被 " << d.layers[0].use_count() << " 个文档共用）\n";
}

int main() {
    Document poster{"海报模板", {std::make_shared<Layer>(Layer{"背景", "白色"})}};

    std::cout << "浅拷贝：\n";
    {
        Document a = poster.shallowCopy();
        a.title = "活动海报";
        a.layers[0]->color = "红色";               // 只想改新文档的背景
        show("新文档", a);
        show("模板", poster);                      // 模板也变成红色了
    }

    poster.layers[0]->color = "白色";              // 把模板改回来
    std::cout << "深拷贝：\n";
    Document b = poster.deepCopy();
    b.title = "活动海报";
    b.layers[0]->color = "红色";
    show("新文档", b);
    show("模板", poster);
}
