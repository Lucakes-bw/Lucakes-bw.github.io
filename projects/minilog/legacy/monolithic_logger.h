#pragma once

// 反面教材：全局单例大泥球版头文件
// 问题：1) 直接把 <fstream>/<mutex>/<sstream>/<iostream>/<thread> 暴露给所有调用方
//       2) 全局唯一实例导致测试状态互相污染
//       3) 任何私有字段变动都会触发全部调用方重编并破坏 ABI
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class MonoLogger {
 public:
  enum Level { Trace = 0, Debug, Info, Warn, Error };

  static MonoLogger& instance() {
    static MonoLogger inst;
    return inst;
  }

  void setLevel(Level l);
  void setOutputPath(const std::string& path);
  void enableConsole(bool on);
  void log(Level l, const std::string& module, const std::string& msg);
  void flush();
  std::size_t lineCount() const;
  void setBenchDiscard(bool on);
  std::size_t checksum() const;
  void resetForTest();

 private:
  MonoLogger() = default;
  ~MonoLogger();
  MonoLogger(const MonoLogger&) = delete;
  MonoLogger& operator=(const MonoLogger&) = delete;

  Level minLevel_ = Info;
  bool consoleOn_ = false;
  bool benchDiscard_ = false;
  std::string outputPath_;
  std::ofstream file_;
  mutable std::mutex mtx_;
  std::vector<std::string> memBuffer_;
  std::string lastLine_;
  std::size_t written_ = 0;
  std::size_t checksum_ = 0;
#ifdef ADD_PRIVATE_FIELD
  int retryCount_ = 0;
#endif
};
