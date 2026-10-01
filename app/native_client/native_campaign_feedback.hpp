#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_campaign_feedback {

// These are deliberately presentation categories. They carry no campaign IDs,
// names, or source-authored text across the simulation/client boundary.
enum class FeedbackKind : std::uint8_t {
  ResearchReport,
  ConstructionComplete,
  ShipComplete,
  SurveyComplete,
  ContactDetected,
  ColonyFounded,
  CombatAlert,
  Count,
};

inline constexpr std::size_t feedback_kind_count =
    static_cast<std::size_t>(FeedbackKind::Count);

struct CampaignFeedbackSummary final {
  std::array<std::uint32_t, feedback_kind_count> counts{};

  [[nodiscard]] std::uint32_t count(FeedbackKind kind) const noexcept;
  [[nodiscard]] bool empty() const noexcept;
};

// Reads only owner-visible event categories. It never propagates raw event
// messages, identities, or foreign campaign state.
[[nodiscard]] CampaignFeedbackSummary collect_campaign_feedback(
    const stellar::core::CampaignFrameResult& frame,
    int observer_civilization_id) noexcept;

struct CampaignFeedbackNotice final {
  FeedbackKind kind{};
  std::uint32_t count{};
  float remaining_seconds{};
};

// Owner-thread presentation state. Notices are coalesced by kind and expire
// after a short real-time lifetime; there can never be more than one notice
// for each bounded category.
class NativeCampaignFeedback final {
 public:
  static constexpr float notice_lifetime_seconds = 6.f;
  static constexpr std::size_t maximum_notices = feedback_kind_count;

  NativeCampaignFeedback() = default;
  NativeCampaignFeedback(const NativeCampaignFeedback&) = delete;
  NativeCampaignFeedback& operator=(const NativeCampaignFeedback&) = delete;

  void publish(const CampaignFeedbackSummary& summary);
  void advance(double real_seconds);
  void reset();
  void set_localization(
      const stellar::engine::LocalizationTable* table) noexcept {
    locale_ = table;
  }
  void render(stellar::native_map::DrawList&, int width, int height) const;

  [[nodiscard]] std::span<const CampaignFeedbackNotice> recent() const;
  [[nodiscard]] CampaignFeedbackSummary counts() const;

 private:
  void require_owner() const;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;

  const stellar::engine::LocalizationTable* locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::array<CampaignFeedbackNotice, maximum_notices> notices_{};
  std::size_t notice_count_{};
  std::uint64_t next_sequence_{};
  std::array<std::uint64_t, maximum_notices> sequences_{};
};

} // namespace stellar::native_campaign_feedback
