#pragma once

#include "native_colony_controller.hpp"

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/surface_construction.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

namespace stellar::native_colony {

struct NativeSurfacePlacementQuote {
  std::uint64_t campaign_generation{}, colony_revision{}, quote_revision{};
  int player_civilization_id{}, system_id{}, body_id{}, colony_id{};
  std::string type_id, building_name;
  float x{}, z{}, normalized_rotation_degrees{};
  int prepared_building_id{};
  std::optional<int> slot_index;
  double authorization_budget_units{}, industry_cost{};
  std::string formatted_authorization;
  bool accepted{};
  std::string message;
};

struct NativeSurfaceRemovalQuote {
  std::uint64_t campaign_generation{}, colony_revision{}, quote_revision{};
  int player_civilization_id{}, system_id{}, body_id{}, colony_id{},
      building_id{};
  std::string type_id, building_name;
  bool accepted{}, cancellation{};
  double refund_budget_units{};
  std::string formatted_refund, message;
};

enum class NativeSurfaceManagementAction {
  UpgradeBuilding,
  RepairBuilding,
  SetEnabled,
  SetPriority,
  UpgradeHub,
};

struct NativeSurfaceManagementQuote {
  std::uint64_t campaign_generation{}, colony_revision{}, quote_revision{};
  int player_civilization_id{}, system_id{}, body_id{}, colony_id{},
      building_id{};
  NativeSurfaceManagementAction action{};
  bool value{}, accepted{};
  std::string action_label, building_name, formatted_authorization,
      description, message;
  double authorization_budget_units{}, industry_cost{};
};

struct NativeSurfaceCommandOutcome {
  bool accepted{};
  std::string message;
};

class NativeSurfaceConstructionController final {
public:
  [[nodiscard]] NativeSurfacePlacementQuote preview_placement(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      const NativeColonyView &, std::string_view type_id, float x, float z,
      float rotation_degrees, std::optional<int> slot = std::nullopt);
  [[nodiscard]] NativeSurfaceRemovalQuote preview_removal(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      const NativeColonyView &, int building_id);
  [[nodiscard]] NativeSurfaceCommandOutcome confirm_placement(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      const NativeSurfacePlacementQuote &);
  [[nodiscard]] NativeSurfaceCommandOutcome confirm_removal(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      const NativeSurfaceRemovalQuote &);
  [[nodiscard]] NativeSurfaceManagementQuote preview_management(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      const NativeColonyView &, NativeSurfaceManagementAction,
      int building_id = 0, bool value = false);
  [[nodiscard]] NativeSurfaceCommandOutcome confirm_management(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      const NativeSurfaceManagementQuote &);
  // Cancels only this controller's detached confirmation token.
  [[nodiscard]] bool cancel_quote(std::uint64_t campaign_generation,
                                  std::uint64_t quote_revision);
  [[nodiscard]] bool is_current_generation(std::uint64_t) const noexcept;

private:
  struct PlacementRecord {
    int system_id{}, body_id{};
    std::uint64_t colony_revision{};
    stellar::core::SurfaceBuildingPlacementAssessment assessment;
  };
  struct RemovalRecord {
    int system_id{}, body_id{};
    std::uint64_t colony_revision{};
    stellar::core::SurfaceBuildingRemovalAssessment assessment;
  };
  struct ManagementSnapshot {
    std::string building_type_id;
    bool building_complete{}, building_enabled{};
    int building_priority{};
    double building_condition{}, building_upgrade_days_remaining{};
    std::optional<std::string> pending_upgrade_type_id;
    int hub_level{};
    double hub_upgrade_days_remaining{};
    [[nodiscard]] bool operator==(const ManagementSnapshot &) const = default;
  };
  struct ManagementRecord {
    int system_id{}, body_id{};
    std::uint64_t colony_revision{};
    NativeSurfaceManagementQuote quote;
    ManagementSnapshot snapshot;
  };
  using QuoteRecord =
      std::variant<PlacementRecord, RemovalRecord, ManagementRecord>;

  [[nodiscard]] static ManagementSnapshot management_snapshot(
      const stellar::core::Colony &, NativeSurfaceManagementAction, int);
  void require_owner() const;
  void bind_generation(std::uint64_t);
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t next_quote_revision_{1};
  std::unordered_map<std::uint64_t, QuoteRecord> quotes_;
};

} // namespace stellar::native_colony
