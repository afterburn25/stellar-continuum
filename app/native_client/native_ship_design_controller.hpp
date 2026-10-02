#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fleet_role.hpp>
#include <stellar/core/ship_components.hpp>
#include <stellar/core/ship_designs.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

// Design Bureau: presentation + command surface for the civilization's
// authored ship designs. All mutations run through the authoritative
// ship_components service on FreshCampaignState — this layer only projects
// and forwards.
namespace stellar::native_ship_design {

struct NativeHullOption {
  std::string id;
  std::string name;
  std::string description;
  stellar::core::FleetRole role{};
  double industry_cost{}, credit_cost{}, strategic_speed{};
  int slot_count{};
  std::vector<stellar::core::ShipComponentSlot> slots;
  std::vector<stellar::core::ShipComponentSlot> required_slots;
  std::optional<std::string> lock_reason;
};

struct NativeComponentOption {
  std::string id;
  std::string name;
  std::string description;
  stellar::core::ShipComponentSlot slot{};
  std::string slot_name;
  double industry_cost{}, credit_cost{};
  std::optional<std::string> lock_reason;
};

struct NativeAuthoredDesignRow {
  std::string id;
  std::string name;
  std::string description;
  std::string hull_id;
  std::string hull_name;
  stellar::core::FleetRole role{};
  std::vector<std::string> component_ids;
  std::vector<std::string> component_names;
  double industry_cost{}, credit_cost{}, strategic_speed{};
  double maximum_leg_range_light_years{}, fuel_endurance_light_years{};
  float sensor_range{};
  double cargo_capacity{};
  double max_shields{}, max_armor{}, max_hull{}, weapon_damage{};
  int crew{};
  bool valid{};
  std::vector<std::string> issues;
  bool can_retire{};
  std::optional<std::string> retire_blocker;
};

// Live composition preview: what the draft would resolve to, plus every
// validation issue the commit path would reject on.
struct NativeShipDesignComposition {
  bool can_commit{};
  std::vector<std::string> issues;
  double industry_cost{}, credit_cost{}, strategic_speed{};
  double maximum_leg_range_light_years{}, fuel_endurance_light_years{};
  float sensor_range{};
  double cargo_capacity{};
  double max_shields{}, max_armor{}, max_hull{}, weapon_damage{};
  int crew{};
};

struct NativeShipDesignView {
  std::uint64_t campaign_generation{};
  std::uint64_t design_revision{};
  int player_civilization_id{};
  std::vector<NativeAuthoredDesignRow> designs;
  std::vector<NativeHullOption> hulls;
  std::vector<NativeComponentOption> components;
};

struct NativeShipDesignCommandOutcome {
  bool accepted{};
  std::string message;
  std::optional<std::string> design_id;
};

class NativeShipDesignController final {
public:
  NativeShipDesignController() = default;

  [[nodiscard]] NativeShipDesignView
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation);
  // Pure preview — validates and resolves the draft without committing.
  [[nodiscard]] NativeShipDesignComposition
  compose(stellar::core::CampaignFrame &, std::string_view hull_id,
          std::span<const std::string> component_ids);
  [[nodiscard]] NativeShipDesignCommandOutcome
  commit(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
         std::uint64_t expected_design_revision, std::string name,
         std::string_view hull_id,
         std::vector<std::string> component_ids);
  [[nodiscard]] NativeShipDesignCommandOutcome
  retire(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
         std::uint64_t expected_design_revision, std::string_view design_id);
  [[nodiscard]] NativeShipDesignCommandOutcome
  rename(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
         std::uint64_t expected_design_revision, std::string_view design_id,
         std::string name, std::string description);

  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

private:
  void require_owner() const;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  const stellar::engine::LocalizationTable *locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t revision_{};
};

} // namespace stellar::native_ship_design
