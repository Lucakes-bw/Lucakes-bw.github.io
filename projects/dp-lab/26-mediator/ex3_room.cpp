// 《中介者模式》练习 3：会议室的音视频协调器——屏幕共享、摄像头、麦克风、录制、低带宽监测 5 个同事的联动
#include <cstdio>
#include <string>

struct RoomMediator {
  virtual ~RoomMediator() = default;
  virtual void notify(const std::string& who) = 0;
};

struct Camera {
  RoomMediator& m;
  bool userWantsOn = true;            // 用户自己想不想开摄像头
  bool active = true;                 // 实际有没有推摄像头流
  int fps = 30;
  void setUserOn(bool v) { userWantsOn = v; m.notify("camera"); }
};

struct ScreenShare {
  RoomMediator& m;
  bool sharing = false;
  void setSharing(bool s) { sharing = s; m.notify("screen"); }
};

struct Microphone {
  RoomMediator& m;
  bool muted = true;
  bool pttDown = false;               // 按住空格发言（Push-to-Talk）
  void setMuted(bool v) { muted = v; m.notify("mic"); }
  void setPtt(bool down) { pttDown = down; m.notify("mic"); }
};

struct Recorder {
  RoomMediator& m;
  bool recording = false;
  std::string mainSource = "摄像头";
  std::string banner = "";
  void setRecording(bool r) { recording = r; m.notify("rec"); }
};

struct NetMonitor {
  RoomMediator& m;
  bool lowBw = false;
  void setLowBandwidth(bool low) { lowBw = low; m.notify("net"); }
};

struct MeetingRoom : RoomMediator {
  Camera cam{*this};
  ScreenShare screen{*this};
  Microphone mic{*this};
  Recorder rec{*this};
  NetMonitor net{*this};

  void notify(const std::string&) override {
    // 1. 低带宽且正在共享屏幕时，自动暂停摄像头保屏幕流；否则尊重用户开关
    cam.active = cam.userWantsOn && !(net.lowBw && screen.sharing);
    // 2. 屏幕共享开启时，摄像头降到 15 fps 省编码算力；低带宽时也降到 15 fps
    cam.fps = (!cam.active) ? 0 : (screen.sharing || net.lowBw ? 15 : 30);
    // 3. 录制的主画面优先取屏幕共享，没有共享时取摄像头，都没有则是音频模式
    rec.mainSource = screen.sharing ? "屏幕共享" : (cam.active ? "摄像头" : "仅音频");
    // 4. 录制开启时显示横幅，并提示当前麦克风是否打开
    bool micLive = !mic.muted || mic.pttDown;
    rec.banner = rec.recording ? (micLive ? "● 录制中（已收音）" : "● 录制中（当前静音）") : "";
  }

  void dump(const char* step) const {
    bool micLive = !mic.muted || mic.pttDown;
    std::printf("%-18s → 摄像头:%s(%2dfps) 屏幕:%s 麦克风:%s 录制主画面:%-8s %s\n",
                step, cam.active ? "开" : "关", cam.fps,
                screen.sharing ? "共享中" : "未共享",
                micLive ? "收音" : "静音",
                rec.mainSource.c_str(), rec.banner.c_str());
  }
};

int main() {
  MeetingRoom room;
  room.notify("init");
  room.dump("1. 进入会议");
  room.screen.setSharing(true);
  room.dump("2. 开始共享屏幕");
  room.rec.setRecording(true);
  room.dump("3. 开启录制");
  room.mic.setPtt(true);
  room.dump("4. 按住空格发言");
  room.net.setLowBandwidth(true);
  room.dump("5. 网络带宽不足");
  room.net.setLowBandwidth(false);
  room.screen.setSharing(false);
  room.dump("6. 恢复网络并停共享");
}
