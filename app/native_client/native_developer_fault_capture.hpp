#pragma once
#include "native_support_service.hpp"
#include "../campaign_diagnostic_monitor.hpp"
#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace stellar::native_developer {
// Host response to the first critical finding of an isolated developer session.
// Keeps one immutable pending capture behind its single writer. Campaign changes
// never discard an already captured report or attribute an old result to a new game.
class NativeDeveloperFaultCapture {
public:
  using Request = stellar::native_support::SupportBundleRequest;
  using ExportState = stellar::native_support::SupportExportState;
  explicit NativeDeveloperFaultCapture(stellar::native_support::NativeSupportService::Writer writer =
      stellar::native_support::export_support_bundle) : service_(std::move(writer)) {}
  void reset() {
    ++generation_; latched_=false; notice_.clear(); path_.clear(); error_.clear();
  }
  [[nodiscard]] bool latched() const { return latched_; }
  [[nodiscard]] bool busy() const { return service_.busy() || pending_.has_value(); }
  [[nodiscard]] const std::string &notice() const { return notice_; }
  [[nodiscard]] const std::string &error() const { return error_; }
  [[nodiscard]] const std::filesystem::path &result() const { return path_; }
  template<class Capture>
  bool observe(stellar::core::CampaignFrame &frame,
               const stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor,
               Capture capture) {
    using namespace stellar::core;
    if (latched_ || !frame.runtime().world().campaign().developer_provenance ||
        !monitor.first_critical()) return false;
    latched_=true;
    frame.clock().set_speed(StrategicSpeed::Paused);
    frame.set_tactical_speed(0.);
    notice_="Critical fault detected. Simulation paused; preparing diagnostic capture.";
    // Preserve an earlier immutable report under backpressure; never replace it.
    if (pending_) {
      fail("Automatic diagnostic queue is full. The simulation is paused; use Export Diagnostics when the writer finishes.");
      return true;
    }
    try {
      auto request=capture();
      request.additional_entries.push_back({"critical-trigger.jsonl",
          stellar::engine::diagnostic_record_json(*monitor.first_critical())+"\n"});
      request.archive_name="Critical-"+request.archive_name;
      pending_=Pending{generation_,std::move(request)};
      service_.record("critical",monitor.first_critical()->message);
      dispatch();
    } catch (const std::exception &e) { fail(e.what()); }
    catch (...) { fail("Unknown failure capturing the critical diagnostic report."); }
    return true;
  }
  void poll() {
    if (service_.poll() && writing_generation_==generation_) {
      if (service_.state()==ExportState::Succeeded) {
        path_=service_.result();
        notice_="Critical diagnostics saved after automatic pause. See the report location in the pause menu.";
      } else fail(service_.error());
    }
    dispatch();
  }
private:
  struct Pending { std::uint64_t generation; Request request; };
  void fail(std::string_view message) {
    auto count=std::min<std::size_t>(message.size(),2048);
    if (count<message.size()) while(count && (static_cast<unsigned char>(message[count])&0xc0)==0x80)--count;
    error_=message.substr(0,count);
    notice_="Critical fault: simulation paused. Automatic diagnostic capture failed: "+error_;
  }
  void dispatch() {
    if (!pending_ || service_.busy()) return;
    writing_generation_=pending_->generation;
    auto request=std::move(pending_->request);pending_.reset();
    if (!service_.request(std::move(request)) && writing_generation_==generation_) fail(service_.error());
  }
  stellar::native_support::NativeSupportService service_;
  std::optional<Pending> pending_;
  std::uint64_t generation_{},writing_generation_{};
  bool latched_{};
  std::string notice_,error_;
  std::filesystem::path path_;
};
} // namespace stellar::native_developer
