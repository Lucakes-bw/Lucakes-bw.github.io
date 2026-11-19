// 《中介者模式》：中介者持有同事的裸指针，同事在局部作用域析构时没注销，下次广播时发生 heap-use-after-free
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Peer;
struct RoomMediator {
  std::vector<Peer*> peers;
  void join(Peer* p) { peers.push_back(p); }
  void broadcast(const Peer* from, const std::string& msg);
};

struct Peer {
  std::string name;
  RoomMediator& room;
  Peer(std::string n, RoomMediator& r) : name(std::move(n)), room(r) {
    room.join(this);                  // 构造时注册，但析构函数忘了从 room 注销
  }
  void say(const std::string& msg) const { room.broadcast(this, msg); }
  void onMessage(const std::string& from, const std::string& msg) const {
    std::printf("  [%s 收到 %s] %s\n", name.c_str(), from.c_str(), msg.c_str());
  }
};

void RoomMediator::broadcast(const Peer* from, const std::string& msg) {
  for (Peer* p : peers)
    if (p != from) p->onMessage(from->name, msg);
}

int main() {
  RoomMediator room;
  auto decoder = std::make_unique<Peer>("解码器", room);
  {
    auto osd = std::make_unique<Peer>("OSD浮层", room);
    decoder->say("第 1 帧就绪");
  }                                   // osd 已销毁，但 room.peers[1] 还是指向它的悬空指针
  decoder->say("第 2 帧就绪");        // 读写已释放的 Peer::name
}
