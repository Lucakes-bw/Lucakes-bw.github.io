// 《命令模式》：命令能写成一行文本——把每条命令（以及撤销、重做）追加到操作日志里，崩溃后从日志重放出同样的时间线
#include <chrono>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct Clip {
    int id, start, in, out;
    double volume = 1.0;
    std::vector<std::string> filters;
    bool operator==(const Clip& o) const {
        return id == o.id && start == o.start && in == o.in && out == o.out && volume == o.volume && filters == o.filters;
    }
};
struct Timeline {
    std::vector<Clip> clips;
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].id == id) return i;
        return -1;
    }
    Clip* find(int id) { return &clips[indexOf(id)]; }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
    virtual std::string serialize() const = 0;   // 一行文本，parse() 能还原
};
class MoveClip : public Command {
    int id_, to_, from_ = 0;
public:
    MoveClip(int id, int to) : id_(id), to_(to) {}
    void execute(Timeline& tl) override { from_ = tl.find(id_)->start; tl.find(id_)->start = to_; }
    void undo(Timeline& tl) override { tl.find(id_)->start = from_; }
    std::string serialize() const override { return "move " + std::to_string(id_) + " " + std::to_string(to_); }
};
class SetVolume : public Command {
    int id_; double v_, old_ = 0;
public:
    SetVolume(int id, double v) : id_(id), v_(v) {}
    void execute(Timeline& tl) override { old_ = tl.find(id_)->volume; tl.find(id_)->volume = v_; }
    void undo(Timeline& tl) override { tl.find(id_)->volume = old_; }
    std::string serialize() const override {
        char buf[64];
        std::snprintf(buf, sizeof buf, "vol %d %.17g", id_, v_);   // %.17g：double 写出去再读回来一位不差
        return buf;
    }
};
class AddFilter : public Command {
    int id_; std::string f_;
public:
    AddFilter(int id, std::string f) : id_(id), f_(std::move(f)) {}
    void execute(Timeline& tl) override { tl.find(id_)->filters.push_back(f_); }
    void undo(Timeline& tl) override { tl.find(id_)->filters.pop_back(); }
    std::string serialize() const override { return "filter " + std::to_string(id_) + " " + f_; }
};
class SplitClip : public Command {
    int id_, at_, newId_;
public:
    SplitClip(int id, int at, int newId) : id_(id), at_(at), newId_(newId) {}
    void execute(Timeline& tl) override {
        int i = tl.indexOf(id_);
        Clip right = tl.clips[i];
        int cut = right.in + (at_ - right.start);
        right.id = newId_; right.start = at_; right.in = cut;
        tl.clips[i].out = cut;
        tl.clips.insert(tl.clips.begin() + i + 1, right);
    }
    void undo(Timeline& tl) override {
        int r = tl.indexOf(newId_);
        tl.find(id_)->out = tl.clips[r].out;
        tl.clips.erase(tl.clips.begin() + r);
    }
    std::string serialize() const override {
        return "split " + std::to_string(id_) + " " + std::to_string(at_) + " " + std::to_string(newId_);
    }
};

// 反序列化：读一行，按第一个词造出对应的命令
std::unique_ptr<Command> parse(const std::string& line) {
    std::istringstream in(line);
    std::string op;
    int id = 0;
    in >> op >> id;
    if (op == "move")  { int to; in >> to; return std::make_unique<MoveClip>(id, to); }
    if (op == "vol")   { double v; in >> v; return std::make_unique<SetVolume>(id, v); }
    if (op == "split") { int at, nid; in >> at >> nid; return std::make_unique<SplitClip>(id, at, nid); }
    if (op == "filter") { std::string f; in >> std::ws; std::getline(in, f); return std::make_unique<AddFilter>(id, f); }
    return nullptr;
}

class History {
public:
    History(Timeline& tl, std::string* journal) : tl_(tl), journal_(journal) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        append(c->serialize());              // 先改内存里的时间线，再追加一行日志
        done_.push_back(std::move(c));
        undone_.clear();
    }
    void undo() {
        if (done_.empty()) return;
        done_.back()->undo(tl_);
        append("undo");
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
    }
    void redo() {
        if (undone_.empty()) return;
        undone_.back()->execute(tl_);
        append("redo");
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
    }
    // 崩溃恢复：从初始状态开始，把日志一行行重放
    void replay(const std::string& log) {
        std::istringstream in(log);
        std::string line;
        auto* saved = journal_;
        journal_ = nullptr;                  // 重放时不再写日志
        while (std::getline(in, line)) {
            if (line == "undo") undo();
            else if (line == "redo") redo();
            else run(parse(line));
        }
        journal_ = saved;
    }

private:
    void append(const std::string& s) { if (journal_) *journal_ += s + "\n"; }
    Timeline& tl_;
    std::string* journal_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

Timeline initial() {
    return {{{1, 0, 0, 3000, 1.0, {} }, {2, 3000, 3000, 8000, 1.0, {} }, {3, 8000, 8000, 10000, 1.0, {} }}};
}

int main() {
    std::string journal;                     // 真实的剪辑器里是磁盘上的文件，每写一行 fflush + fsync
    Timeline tl = initial();
    {
        History h(tl, &journal);
        h.run(std::make_unique<MoveClip>(3, 9000));
        h.run(std::make_unique<SetVolume>(2, 0.1 * 3));
        h.run(std::make_unique<AddFilter>(1, "fade=t=in:d=1"));
        h.run(std::make_unique<SplitClip>(2, 5000, 4));
        h.undo();
        h.undo();
        h.redo();
        h.run(std::make_unique<AddFilter>(2, "eq=brightness=0.06:saturation=1.2"));
    }                                        // 「崩溃」：History 和撤销栈都没了，只剩 tl 用来对照
    std::printf("操作日志（%zu 字节）：\n%s", journal.size(), journal.c_str());

    Timeline recovered = initial();
    History h2(recovered, nullptr);
    h2.replay(journal);
    std::printf("重放日志后的时间线和崩溃前%s\n", recovered.clips == tl.clips ? "完全一致" : "不一致！");

    // 1000 次操作的日志有多大，重放要多久
    std::string big;
    Timeline t3 = initial();
    {
        History h(t3, &big);
        for (int k = 0; k < 1000; ++k) {
            int id = k % 3 + 1;
            if (k % 4 == 3) h.run(std::make_unique<AddFilter>(id, "eq=brightness=0.06:saturation=1.2"));
            else if (k % 2) h.run(std::make_unique<SetVolume>(id, 0.1 * (k % 7 + 1)));
            else h.run(std::make_unique<MoveClip>(id, t3.find(id)->start + 10));
        }
    }
    Timeline t4 = initial();
    History h4(t4, nullptr);
    auto s = std::chrono::steady_clock::now();
    h4.replay(big);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
    std::printf("1000 条命令的日志 %zu 字节（平均每条 %.1f 字节），重放 %.2f ms，结果%s\n", big.size(),
                big.size() / 1000.0, ms, t4.clips == t3.clips ? "一致" : "不一致！");
}
