#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

#define MINILOG_CHECK(cond)                                                     \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      std::exit(1);                                                             \
    }                                                                           \
  } while (0)

namespace minilog_test {

class TempDirGuard {
 public:
  explicit TempDirGuard(const char* prefix) {
    std::string tmpl = "/tmp/";
    tmpl += prefix;
    tmpl += "_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* res = ::mkdtemp(buf.data());
    if (res) {
      path_ = res;
    } else {
      path_ = std::filesystem::temp_directory_path() / prefix;
      std::filesystem::create_directories(path_);
    }
  }

  ~TempDirGuard() {
    std::error_code ec;
    if (!path_.empty()) std::filesystem::remove_all(path_, ec);
  }

  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;

  std::string file(const std::string& name) const {
    return (path_ / name).string();
  }

 private:
  std::filesystem::path path_;
};

}  // namespace minilog_test
