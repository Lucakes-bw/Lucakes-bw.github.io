#pragma once

#include <type_traits>
#include <utility>

namespace minilog {

template <typename F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F&& fn) noexcept(std::is_nothrow_move_constructible_v<F>)
      : fn_(std::move(fn)), active_(true) {}

  explicit ScopeGuard(const F& fn) noexcept(std::is_nothrow_copy_constructible_v<F>)
      : fn_(fn), active_(true) {}

  ScopeGuard(ScopeGuard&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
      : fn_(std::move(other.fn_)), active_(other.active_) {
    other.dismiss();
  }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard& operator=(ScopeGuard&&) = delete;

  ~ScopeGuard() noexcept {
    if (active_) fn_();
  }

  void dismiss() noexcept { active_ = false; }
  bool active() const noexcept { return active_; }

 private:
  F fn_;
  bool active_;
};

template <typename F>
ScopeGuard<std::decay_t<F>> makeScopeGuard(F&& fn) {
  return ScopeGuard<std::decay_t<F>>(std::forward<F>(fn));
}

}  // namespace minilog
