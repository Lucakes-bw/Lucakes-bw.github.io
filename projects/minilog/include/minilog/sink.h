#pragma once

#include "minilog/level.h"
#include "minilog/pattern.h"
#include "minilog/record.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace minilog {

// 1. 模板方法模式（NVI）：固定加锁 -> 级别检查 -> 格式化 -> 落盘骨架
class BaseSink {
 public:
  virtual ~BaseSink() = default;

  void log(const LogRecord& rec);
  void flush();

  void setLevel(Level minLevel);
  Level level() const;
  void setPattern(std::string_view pattern);

 protected:
  explicit BaseSink(Level minLevel = Level::Trace,
                    std::string_view pattern = "[%T] [%L] [%n] %v");

 private:
  virtual void writeImpl(std::string_view formatted, const LogRecord& rec) = 0;
  virtual void flushImpl() = 0;

  mutable std::mutex mtx_;
  Level minLevel_;
  PatternFormatter formatter_;
  std::string scratch_;
};

// 2. 策略模式：日志文件滚动条件
class RotationPolicy {
 public:
  virtual ~RotationPolicy() = default;
  virtual bool shouldRotate(std::size_t nextTotalBytes, std::size_t linesWritten,
                            uint64_t nowMs, uint64_t openedMs) const = 0;
};

class SizeRotation final : public RotationPolicy {
 public:
  explicit SizeRotation(std::size_t maxBytes) : maxBytes_(maxBytes) {}
  bool shouldRotate(std::size_t nextTotalBytes, std::size_t,
                    uint64_t, uint64_t) const override {
    return maxBytes_ > 0 && nextTotalBytes > maxBytes_;
  }

 private:
  std::size_t maxBytes_;
};

class LineCountRotation final : public RotationPolicy {
 public:
  explicit LineCountRotation(std::size_t maxLines) : maxLines_(maxLines) {}
  bool shouldRotate(std::size_t, std::size_t linesWritten,
                    uint64_t, uint64_t) const override {
    return maxLines_ > 0 && linesWritten >= maxLines_;
  }

 private:
  std::size_t maxLines_;
};

class TimeRotation final : public RotationPolicy {
 public:
  explicit TimeRotation(uint64_t intervalMs) : intervalMs_(intervalMs) {}
  bool shouldRotate(std::size_t, std::size_t,
                    uint64_t nowMs, uint64_t openedMs) const override {
    return intervalMs_ > 0 && nowMs > openedMs && (nowMs - openedMs) >= intervalMs_;
  }

 private:
  uint64_t intervalMs_;
};

// 内置具体 Sink
class ConsoleSink final : public BaseSink {
 public:
  explicit ConsoleSink(Level minLevel = Level::Trace, bool useStderr = false,
                       std::string_view pattern = "[%T] [%L] [%n] %v");

 private:
  void writeImpl(std::string_view formatted, const LogRecord& rec) override;
  void flushImpl() override;

  std::FILE* stream_;
};

class FileSink final : public BaseSink {
 public:
  FileSink(std::string path, Level minLevel = Level::Trace,
           std::size_t bufferBytes = 65536,
           std::string_view pattern = "[%T] [%L] [%n] %v");
  ~FileSink() override;

  bool isOpen() const noexcept { return file_ != nullptr; }
  const std::string& path() const noexcept { return path_; }

 private:
  void writeImpl(std::string_view formatted, const LogRecord& rec) override;
  void flushImpl() override;
  void flushBufferLocked();

  std::string path_;
  std::FILE* file_ = nullptr;
  std::size_t bufferCap_;
  std::string buffer_;
};

class RotatingFileSink final : public BaseSink {
 public:
  RotatingFileSink(std::string basePath, std::unique_ptr<RotationPolicy> policy,
                   std::size_t maxFiles = 3, Level minLevel = Level::Trace,
                   std::string_view pattern = "[%T] [%L] [%n] %v");
  ~RotatingFileSink() override;

  bool isOpen() const noexcept { return file_ != nullptr; }
  std::size_t rotateCount() const;

 private:
  void writeImpl(std::string_view formatted, const LogRecord& rec) override;
  void flushImpl() override;
  void rotateLocked(uint64_t nowMs);
  void openFreshLocked(uint64_t nowMs);

  std::string basePath_;
  std::unique_ptr<RotationPolicy> policy_;
  std::size_t maxFiles_;
  std::FILE* file_ = nullptr;
  std::size_t bytesWritten_ = 0;
  std::size_t linesWritten_ = 0;
  uint64_t openedMs_ = 0;
  std::size_t rotateCount_ = 0;
};

class MemorySink final : public BaseSink {
 public:
  explicit MemorySink(Level minLevel = Level::Trace,
                      std::string_view pattern = "[%T] [%L] [%n] %v");

  std::vector<std::string> lines() const;
  std::size_t count() const;
  void clear();

 private:
  void writeImpl(std::string_view formatted, const LogRecord& rec) override;
  void flushImpl() override;

  mutable std::mutex dataMtx_;
  std::vector<std::string> lines_;
};

// 允许 std::shared_ptr<BaseSink子类> 直接放进 AnySink
template <typename U, typename = std::enable_if_t<std::is_base_of_v<BaseSink, U>>>
inline void write_sink(const std::shared_ptr<U>& s, const LogRecord& rec) {
  if (s) s->log(rec);
}

template <typename U, typename = std::enable_if_t<std::is_base_of_v<BaseSink, U>>>
inline void flush_sink(const std::shared_ptr<U>& s) {
  if (s) s->flush();
}

namespace detail {

template <int N>
struct PriorityTag : PriorityTag<N - 1> {};
template <>
struct PriorityTag<0> {};

template <typename T>
auto callWrite(T& obj, const LogRecord& rec, PriorityTag<2>)
    -> decltype(write_sink(obj, rec), void()) {
  write_sink(obj, rec);
}

template <typename T>
auto callWrite(T& obj, const LogRecord& rec, PriorityTag<1>)
    -> decltype(obj.log(rec), void()) {
  obj.log(rec);
}

template <typename T>
auto callWrite(T& obj, const LogRecord& rec, PriorityTag<0>)
    -> decltype(obj.write(rec), void()) {
  obj.write(rec);
}

template <typename T>
auto callFlush(T& obj, PriorityTag<2>) -> decltype(flush_sink(obj), void()) {
  flush_sink(obj);
}

template <typename T>
auto callFlush(T& obj, PriorityTag<1>) -> decltype(obj.flush(), void()) {
  obj.flush();
}

template <typename T>
void callFlush(T&, PriorityTag<0>) noexcept {}

}  // namespace detail

// 3. 类型擦除（48 B SBO + 手写 VTable）：非侵入式值语义 Sink 包装器
class AnySink {
 public:
  static constexpr std::size_t kSboSize  = 48;
  static constexpr std::size_t kSboAlign = alignof(std::max_align_t);

 private:
  struct VTable {
    void (*log)(void*, const LogRecord&);
    void (*flush)(void*);
    void (*destroy)(void*) noexcept;
    void (*move)(void* src, void* dst) noexcept;
    bool isInline;
  };

  template <typename T>
  static constexpr bool kFitsInSbo =
      sizeof(T) <= kSboSize && alignof(T) <= kSboAlign && std::is_nothrow_move_constructible_v<T>;

  template <typename T>
  static const VTable* vtFor() noexcept {
    if constexpr (kFitsInSbo<T>) {
      static const VTable tbl{
          [](void* p, const LogRecord& rec) {
            detail::callWrite(*std::launder(reinterpret_cast<T*>(p)), rec, detail::PriorityTag<2>{});
          },
          [](void* p) {
            detail::callFlush(*std::launder(reinterpret_cast<T*>(p)), detail::PriorityTag<2>{});
          },
          [](void* p) noexcept { std::launder(reinterpret_cast<T*>(p))->~T(); },
          [](void* src, void* dst) noexcept {
            ::new (dst) T(std::move(*std::launder(reinterpret_cast<T*>(src))));
          },
          true};
      return &tbl;
    } else {
      static const VTable tbl{
          [](void* p, const LogRecord& rec) {
            T* ptr = *std::launder(reinterpret_cast<T**>(p));
            detail::callWrite(*ptr, rec, detail::PriorityTag<2>{});
          },
          [](void* p) {
            T* ptr = *std::launder(reinterpret_cast<T**>(p));
            detail::callFlush(*ptr, detail::PriorityTag<2>{});
          },
          [](void* p) noexcept { delete *std::launder(reinterpret_cast<T**>(p)); },
          [](void* src, void* dst) noexcept {
            T*& sp = *std::launder(reinterpret_cast<T**>(src));
            ::new (dst) T*(sp);
            sp = nullptr;
          },
          false};
      return &tbl;
    }
  }

  alignas(kSboAlign) std::byte storage_[kSboSize]{};
  const VTable* vptr_ = nullptr;

 public:
  AnySink() noexcept = default;

  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AnySink>>>
  AnySink(T sink) : vptr_(vtFor<std::decay_t<T>>()) {
    using U = std::decay_t<T>;
    if constexpr (kFitsInSbo<U>) {
      ::new (storage_) U(std::move(sink));
    } else {
      ::new (storage_) U*(new U(std::move(sink)));
    }
  }

  AnySink(AnySink&& other) noexcept : vptr_(other.vptr_) {
    if (vptr_) {
      vptr_->move(other.storage_, storage_);
      vptr_->destroy(other.storage_);
      other.vptr_ = nullptr;
    }
  }

  AnySink& operator=(AnySink&& other) noexcept {
    if (this != &other) {
      if (vptr_) {
        vptr_->destroy(storage_);
        vptr_ = nullptr;
      }
      vptr_ = other.vptr_;
      if (vptr_) {
        vptr_->move(other.storage_, storage_);
        vptr_->destroy(other.storage_);
        other.vptr_ = nullptr;
      }
    }
    return *this;
  }

  AnySink(const AnySink&) = delete;
  AnySink& operator=(const AnySink&) = delete;

  ~AnySink() {
    if (vptr_) vptr_->destroy(storage_);
  }

  static AnySink fromShared(std::shared_ptr<BaseSink> sink) {
    return AnySink(std::move(sink));
  }

  explicit operator bool() const noexcept { return vptr_ != nullptr; }
  bool isInline() const noexcept { return vptr_ != nullptr && vptr_->isInline; }

  void log(const LogRecord& rec) {
    if (vptr_) vptr_->log(storage_, rec);
  }

  void flush() {
    if (vptr_) vptr_->flush(storage_);
  }
};

// 4. 工厂注册表：按名字创建内置或用户扩展 Sink
struct SinkSpec {
  std::string type;
  Level minLevel          = Level::Trace;
  std::string pattern     = "[%T] [%L] [%n] %v";
  std::string path;
  bool useStderr          = false;
  std::size_t bufferBytes = 65536;
  std::size_t maxBytes    = 1024 * 1024;
  std::size_t maxLines    = 0;
  uint64_t intervalMs     = 0;
  std::size_t maxFiles    = 3;
};

class SinkFactory {
 public:
  using CreatorFn = std::function<bool(const SinkSpec&, AnySink&, std::string& err)>;

  SinkFactory();
  void registerType(std::string typeName, CreatorFn fn);
  bool create(const SinkSpec& spec, AnySink& out, std::string& err) const;
  bool hasType(const std::string& typeName) const;

 private:
  std::unordered_map<std::string, CreatorFn> creators_;
};

}  // namespace minilog
