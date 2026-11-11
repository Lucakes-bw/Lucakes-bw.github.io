// 《命令模式》练习 2：加一个修剪命令 TrimClip（拖动片段右边缘改长度），能撤销，同一次拖动合并成一条
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip { int id, start, len; };
struct Timeline {
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
    virtual bool mergeWith(const Command&) { return false; }
};

class TrimClip : public Command {
public:
    TrimClip(int id, int len, int gesture) : id_(id), len_(len), gesture_(gesture) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        old_ = c->len;
        c->len = len_ < 100 ? 100 : len_;    // 最短 100ms；撤销用的是 old_，不受这里截断的影响
    }
    void undo(Timeline& tl) override { tl.find(id_)->len = old_; }
    bool mergeWith(const Command& next) override {
        auto* t = dynamic_cast<const TrimClip*>(&next);
        if (!t || t->id_ != id_ || t->gesture_ != gesture_) return false;
        len_ = t->len_;                       // 保留第一条的 old_
        return true;
    }

private:
    int id_, len_, gesture_, old_ = 0;
};

class History {
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        undone_.clear();
        if (!done_.empty() && done_.back()->mergeWith(*c)) return;
        done_.push_back(std::move(c));
    }
    void undo() {
        if (done_.empty()) return;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
    }
    void redo() {
        if (undone_.empty()) return;
        undone_.back()->execute(tl_);
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
    }
    std::size_t size() const { return done_.size(); }

private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

int main() {
    Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}}};
    History h(tl);
    for (int len = 4900; len >= 2000; len -= 100) h.run(std::make_unique<TrimClip>(2, len, 1));  // 第 1 次拖动
    for (int len = 2100; len <= 2500; len += 100) h.run(std::make_unique<TrimClip>(2, len, 2));  // 第 2 次拖动
    std::printf("两次拖动后 #2 长 %dms，撤销栈 %zu 条\n", tl.find(2)->len, h.size());
    h.undo();
    std::printf("撤销一次：%dms\n", tl.find(2)->len);
    h.undo();
    std::printf("再撤销  ：%dms\n", tl.find(2)->len);
    h.redo();
    std::printf("重做    ：%dms\n", tl.find(2)->len);
}
