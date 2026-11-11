// 《命令模式》：把剪辑操作封装成命令对象，每个命令自己记住怎么撤销；History 维护撤销栈和重做栈
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip {
    int id;
    std::string source;
    int start, in, out;              // 时间线上的起点；素材里的入点、出点（毫秒）
    double volume = 1.0;
    std::vector<std::string> filters;
    int end() const { return start + out - in; }
};

struct Timeline {                    // 接收者（Receiver）：真正被改的对象
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].id == id) return i;
        return -1;
    }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline& tl) = 0;
    virtual void undo(Timeline& tl) = 0;
    virtual std::string name() const = 0;
};

class MoveClip : public Command {
public:
    MoveClip(int id, int to) : id_(id), to_(to) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        from_ = c->start;            // 执行时记下旧值，撤销时用
        c->start = to_;
    }
    void undo(Timeline& tl) override { tl.find(id_)->start = from_; }
    std::string name() const override { return "移动 #" + std::to_string(id_); }

private:
    int id_, to_, from_ = 0;
};

class SetVolume : public Command {
public:
    SetVolume(int id, double v) : id_(id), v_(v) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        old_ = c->volume;
        c->volume = v_;
    }
    void undo(Timeline& tl) override { tl.find(id_)->volume = old_; }
    std::string name() const override { return "调音量 #" + std::to_string(id_); }

private:
    int id_;
    double v_, old_ = 0;
};

class AddFilter : public Command {
public:
    AddFilter(int id, std::string f) : id_(id), f_(std::move(f)) {}
    void execute(Timeline& tl) override { tl.find(id_)->filters.push_back(f_); }
    void undo(Timeline& tl) override { tl.find(id_)->filters.pop_back(); }
    std::string name() const override { return "加滤镜 #" + std::to_string(id_); }

private:
    int id_;
    std::string f_;
};

// 剪切：在时间线上的 at 处把片段一分为二，后半段的 id 在构造时就定好，重做时还是这个 id
class SplitClip : public Command {
public:
    SplitClip(int id, int at, int newId) : id_(id), at_(at), newId_(newId) {}
    void execute(Timeline& tl) override {
        int i = tl.indexOf(id_);
        Clip right = tl.clips[i];
        int cut = tl.clips[i].in + (at_ - tl.clips[i].start);
        right.id = newId_;
        right.start = at_;
        right.in = cut;
        tl.clips[i].out = cut;
        tl.clips.insert(tl.clips.begin() + i + 1, right);
    }
    void undo(Timeline& tl) override {
        int r = tl.indexOf(newId_);
        tl.find(id_)->out = tl.clips[r].out;
        tl.clips.erase(tl.clips.begin() + r);
    }
    std::string name() const override { return "剪切 #" + std::to_string(id_); }

private:
    int id_, at_, newId_;
};

class History {                      // 调用者（Invoker）：只认识 Command 接口
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        done_.push_back(std::move(c));
        undone_.clear();             // 做了新操作，重做栈作废
    }
    bool undo() {
        if (done_.empty()) return false;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
        return true;
    }
    bool redo() {
        if (undone_.empty()) return false;
        undone_.back()->execute(tl_);
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
        return true;
    }
    void print() const {
        std::printf("   撤销栈：");
        for (auto& c : done_) std::printf("[%s] ", c->name().c_str());
        std::printf("\n   重做栈：");
        for (auto& c : undone_) std::printf("[%s] ", c->name().c_str());
        std::printf("\n");
    }

private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

void show(const Timeline& tl) {
    for (auto& c : tl.clips) {
        std::printf("   #%d %5d-%5dms 音量 %.1f", c.id, c.start, c.end(), c.volume);
        for (auto& f : c.filters) std::printf(" [%s]", f.c_str());
        std::printf("\n");
    }
}

int main() {
    Timeline tl{{{1, "bbb-10s.mp4", 0, 0, 3000, 1.0, {} },
                 {2, "bbb-10s.mp4", 3000, 3000, 8000, 1.0, {} },
                 {3, "bbb-10s.mp4", 8000, 8000, 10000, 1.0, {} }}};
    History h(tl);
    h.run(std::make_unique<MoveClip>(3, 9000));
    h.run(std::make_unique<SetVolume>(2, 0.5));
    h.run(std::make_unique<AddFilter>(1, "fade=t=in:d=1"));
    h.run(std::make_unique<SplitClip>(2, 5000, 4));
    std::printf("执行 4 条命令：\n");
    show(tl);
    h.print();

    h.undo();
    h.undo();
    std::printf("撤销两次（先撤剪切，再撤滤镜）：\n");
    show(tl);
    h.print();

    h.redo();
    std::printf("重做一次（滤镜回来了）：\n");
    show(tl);

    h.run(std::make_unique<SetVolume>(1, 0.8));
    std::printf("再做一条新命令，重做栈被清空，redo() 返回 %s：\n", h.redo() ? "true" : "false");
    h.print();
}
