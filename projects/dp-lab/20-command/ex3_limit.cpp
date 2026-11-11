// 《命令模式》练习 3：撤销历史最多保留 N 步，并记住「保存点」，标题栏据此显示有没有未保存的改动
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>

struct Timeline { int volume = 10; };

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
};
class SetVolume : public Command {
    int v_, old_ = 0;
public:
    explicit SetVolume(int v) : v_(v) {}
    void execute(Timeline& tl) override { old_ = tl.volume; tl.volume = v_; }
    void undo(Timeline& tl) override { tl.volume = old_; }
};

class History {
public:
    History(Timeline& tl, std::size_t limit) : tl_(tl), limit_(limit) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        if (clean_ > (long)done_.size()) clean_ = -1;   // 保存点在被清掉的重做栈里：再也回不去了
        undone_.clear();
        done_.push_back(std::move(c));
        if (done_.size() > limit_) {                   // 超过上限，丢掉最老的一条
            done_.pop_front();
            if (clean_ >= 0) --clean_;                 // 保存点跟着左移；从 0 变成 -1 也是回不去了
        }
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
    void setClean() { clean_ = (long)done_.size(); }   // 保存文件时调用
    bool isClean() const { return clean_ == (long)done_.size(); }

private:
    Timeline& tl_;
    std::size_t limit_;
    std::deque<std::unique_ptr<Command>> done_;        // 要从前面删，用 deque
    std::vector<std::unique_ptr<Command>> undone_;
    long clean_ = 0;                                   // 保存点 = 那时撤销栈的长度；-1 表示回不去了
};

void title(const History& h, const Timeline& tl, const char* what) {
    std::printf("%s：音量 %d，标题栏「剪辑工程%s」\n", what, tl.volume, h.isClean() ? "" : " *");
}

int main() {
    Timeline tl;
    History h(tl, 3);
    title(h, tl, "打开文件");
    h.run(std::make_unique<SetVolume>(11));
    h.run(std::make_unique<SetVolume>(12));
    title(h, tl, "改两次");
    h.setClean();
    title(h, tl, "保存");
    h.undo();
    title(h, tl, "撤销一次");
    h.redo();
    title(h, tl, "重做，回到保存点");
    h.undo();
    h.run(std::make_unique<SetVolume>(20));
    title(h, tl, "撤销一次再改成 20");
    h.undo();
    title(h, tl, "再撤销一次（保存点所在的分支已经没了）");

    Timeline t2;
    History h2(t2, 3);
    h2.setClean();
    for (int v = 11; v <= 14; ++v) h2.run(std::make_unique<SetVolume>(v));
    h2.undo(); h2.undo(); h2.undo(); h2.undo();
    title(h2, t2, "\n新工程：保存后改 4 次，再撤销到底（只留 3 步）");
}
