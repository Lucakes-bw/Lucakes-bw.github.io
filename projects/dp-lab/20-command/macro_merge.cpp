// 《命令模式》：宏命令（波纹删除 = 删除片段 + 后面的片段整体左移）和命令合并（一次拖动的上百个移动合成一条）
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip {
    int id;
    int start, len;                  // 只保留这一节用得到的字段（毫秒）
};

struct Timeline {
    std::vector<Clip> clips;
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].id == id) return i;
        return -1;
    }
    Clip* find(int id) { int i = indexOf(id); return i < 0 ? nullptr : &clips[i]; }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline& tl) = 0;
    virtual void undo(Timeline& tl) = 0;
    virtual std::string name() const = 0;
    // 能不能把紧跟在后面的 next 并进自己；能就并进来并返回 true（next 已经执行过了）
    virtual bool mergeWith(const Command&) { return false; }
};

class MoveClip : public Command {
public:
    // gesture：同一次拖动（按下鼠标到松开）里产生的移动命令带同一个编号；0 表示不参与合并
    MoveClip(int id, int to, int gesture = 0) : id_(id), to_(to), gesture_(gesture) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        from_ = c->start;
        c->start = to_;
    }
    void undo(Timeline& tl) override { tl.find(id_)->start = from_; }
    std::string name() const override {
        return "移动 #" + std::to_string(id_) + " " + std::to_string(from_) + "→" + std::to_string(to_);
    }
    bool mergeWith(const Command& next) override {
        auto* m = dynamic_cast<const MoveClip*>(&next);
        if (!m || gesture_ == 0 || m->gesture_ != gesture_ || m->id_ != id_) return false;
        to_ = m->to_;                // 起点 from_ 用自己的，终点用最新的
        return true;
    }

private:
    int id_, to_, gesture_, from_ = 0;
};

class DeleteClip : public Command {
public:
    explicit DeleteClip(int id) : id_(id) {}
    void execute(Timeline& tl) override {
        index_ = tl.indexOf(id_);
        saved_ = tl.clips[index_];   // 删掉之前把整个片段存下来
        tl.clips.erase(tl.clips.begin() + index_);
    }
    void undo(Timeline& tl) override { tl.clips.insert(tl.clips.begin() + index_, saved_); }
    std::string name() const override { return "删除 #" + std::to_string(id_); }

private:
    int id_, index_ = 0;
    Clip saved_{};
};

// 宏命令：本身也是 Command，里面装着一串命令；执行按顺序，撤销倒着来
class MacroCommand : public Command {
public:
    explicit MacroCommand(std::string name) : name_(std::move(name)) {}
    void add(std::unique_ptr<Command> c) { cmds_.push_back(std::move(c)); }
    void execute(Timeline& tl) override {
        for (auto& c : cmds_) c->execute(tl);
    }
    void undo(Timeline& tl) override {
        for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) (*it)->undo(tl);
    }
    std::string name() const override { return name_ + "（" + std::to_string(cmds_.size()) + " 条）"; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> cmds_;
};

// 波纹删除：删掉一个片段，后面的片段都往前挪，补上空出来的位置
std::unique_ptr<Command> rippleDelete(const Timeline& tl, int id) {
    auto macro = std::make_unique<MacroCommand>("波纹删除 #" + std::to_string(id));
    const Clip& del = tl.clips[tl.indexOf(id)];
    macro->add(std::make_unique<DeleteClip>(id));
    for (auto& c : tl.clips)
        if (c.start > del.start) macro->add(std::make_unique<MoveClip>(c.id, c.start - del.len));
    return macro;
}

class History {
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        undone_.clear();
        if (!done_.empty() && done_.back()->mergeWith(*c)) return;   // 并进上一条了，这条丢掉
        done_.push_back(std::move(c));
    }
    bool undo() {
        if (done_.empty()) return false;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
        return true;
    }
    std::size_t size() const { return done_.size(); }
    std::string top() const { return done_.empty() ? "（空）" : done_.back()->name(); }

private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

void show(const char* title, const Timeline& tl) {
    std::printf("%s", title);
    for (auto& c : tl.clips) std::printf(" #%d[%d,%d)", c.id, c.start, c.start + c.len);
    std::printf("\n");
}

int main() {
    // 1. 拖动：鼠标从 8000ms 拖到 9200ms，每 10ms 来一个 mousemove 事件
    for (int gesture : {0, 1}) {
        Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}, {3, 8000, 2000}}};
        History h(tl);
        for (int x = 8010; x <= 9200; x += 10) h.run(std::make_unique<MoveClip>(3, x, gesture));
        std::printf("%s：拖动产生 120 条移动命令，撤销栈里有 %zu 条，栈顶是「%s」\n",
                    gesture ? "合并" : "不合并", h.size(), h.top().c_str());
        int presses = 0;
        while (tl.find(3)->start != 8000 && h.undo()) ++presses;
        std::printf("   要按 %d 次 Ctrl+Z 才能回到 8000ms\n", presses);
    }

    // 2. 宏命令：波纹删除 #2
    Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}, {3, 8000, 2000}, {4, 10000, 1500}}};
    History h(tl);
    show("删除前      ：", tl);
    h.run(rippleDelete(tl, 2));
    show("波纹删除 #2 ：", tl);
    std::printf("   撤销栈 %zu 条：「%s」\n", h.size(), h.top().c_str());
    h.undo();
    show("撤销一次    ：", tl);
}
