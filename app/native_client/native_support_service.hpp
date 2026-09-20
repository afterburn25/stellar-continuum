#pragma once

#include "native_support.hpp"
#include <deque>
#include <functional>
#include <future>
#include <string>
#include <string_view>

namespace stellar::native_support {
enum class SupportExportState { Idle, Working, Succeeded, Failed };

// Main-thread service: one immutable request owns one background writer.
// No automatic retries, pending queue, simulation access or per-frame disk IO.
class NativeSupportService final {
 public:
  using Writer = std::function<std::filesystem::path(const SupportBundleRequest&)>;
  explicit NativeSupportService(Writer writer=export_support_bundle);
  void record(std::string_view category,std::string_view message);
  [[nodiscard]] bool request(SupportBundleRequest request);
  [[nodiscard]] bool poll();
  void report_capture_failure(std::string_view);
  [[nodiscard]] SupportExportState state() const noexcept { return state_; }
  [[nodiscard]] bool busy() const noexcept { return state_==SupportExportState::Working; }
  [[nodiscard]] const std::filesystem::path& result() const noexcept { return result_; }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }
  [[nodiscard]] std::string log_snapshot() const;
 private:
  Writer writer_;
  std::deque<std::string> log_;
  std::future<std::filesystem::path> worker_;
  SupportExportState state_{};
  std::filesystem::path result_;
  std::string error_;
};
}
