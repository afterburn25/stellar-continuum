#pragma once

#include "native_colony_controller.hpp"

#include <stellar/core/campaign_frame.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_colony {
struct NativeOutpostFreightPreview {
  std::uint64_t campaign_generation{}, revision{};
  int player_civilization_id{}, colony_id{}, body_id{}, system_id{}, fleet_id{},
      home_colony_id{};
  bool accepted{};
  std::string message, fleet_name, outpost_name, home_name;
  double cargo_capacity{}, stored_materials{}, extraction_per_day{};
};
struct NativeOutpostFreightOutcome {
  bool accepted{};
  std::string message;
};

class NativeOutpostFreightController final {
public:
  [[nodiscard]] NativeOutpostFreightPreview
  preview(stellar::core::CampaignFrame &, std::uint64_t,
          const NativeColonyView &);
  [[nodiscard]] NativeOutpostFreightOutcome
  issue(stellar::core::CampaignFrame &, std::uint64_t, std::uint64_t);
  void clear() noexcept;
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

private:
  struct DecisionSnapshot {
    stellar::core::FleetState fleet_before;
    stellar::core::FleetState fleet_after_preflight;
    int home_body_id{};
    std::string home_name, outpost_name;
    double stored_materials{}, extraction_per_day{}, storage_capacity{},
        remaining_materials{};
  };
  struct HeldQuote {
    NativeOutpostFreightPreview preview;
    DecisionSnapshot decision;
  };
  void require_owner() const;
  void bind_generation(std::uint64_t);
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] NativeOutpostFreightOutcome stale() const;
  const stellar::engine::LocalizationTable *locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t next_revision_{1};
  std::optional<HeldQuote> quote_;
};
} // namespace stellar::native_colony
