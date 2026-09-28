#pragma once

#include <stellar/core/adaptive_research_outcome_snapshot.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct AdaptiveResearchCivilizationStart {
  int civilization_id{};
  std::string species_id;
  std::string reference_profile_id;
  std::string applicability_context_id;
};

struct AdaptiveResearchProjectFundingState {
  std::string node_id;
  double reserved_milestone_credits{};
  double consumed_milestone_credits{};
  double authorization_credits{};
};

struct AdaptiveResearchProjectFundingSnapshot {
  std::string node_id;
  double reserved_milestone_credits{};
  double consumed_milestone_credits{};
  double authorization_credits{};
};

// Campaign-owned player intent. Execution remains in the normal research/funding
// pipeline; these ordered IDs never reserve resources or bypass discovery.
struct AdaptiveResearchPlan {
  std::vector<std::string> favorites;
  std::vector<std::string> queue;
  bool suggestions{true};
  bool operator==(const AdaptiveResearchPlan &) const = default;
};

enum class ResearchPlanCommand {
  AddFavorite, RemoveFavorite, Enqueue, RemoveQueued, MoveUp, MoveDown,
  SuggestionsOn, SuggestionsOff
};

struct AdaptiveResearchCampaignCivilizationSnapshot {
  int civilization_id{};
  std::string species_id;
  std::string reference_profile_id;
  std::string applicability_context_id;
  AdaptiveResearchStateSnapshotV5 research;
  std::vector<AdaptiveResearchProjectFundingSnapshot> project_funding;
  AdaptiveResearchPlan plan;
};

struct AdaptiveResearchCampaignSnapshot {
  int schema_version{};
  std::string catalog_id;
  std::vector<AdaptiveResearchCampaignCivilizationSnapshot> civilizations;
};

class AdaptiveResearchCampaignMissingState final : public std::out_of_range {
public:
  explicit AdaptiveResearchCampaignMissingState(std::string message);
};

class AdaptiveResearchCampaignDataError final : public std::runtime_error {
public:
  explicit AdaptiveResearchCampaignDataError(std::string message);
};

class AdaptiveResearchCampaignOperationError final : public std::runtime_error {
public:
  explicit AdaptiveResearchCampaignOperationError(std::string message);
};

class AdaptiveResearchCampaignArgumentError final : public std::invalid_argument {
public:
  explicit AdaptiveResearchCampaignArgumentError(std::string message);
};

namespace detail {
class AdaptiveResearchCampaignStateAccess;
}

// Owns one research state, starting identity, and sparse funding collection per
// civilization while borrowing a stable strategic runtime. The runtime must
// outlive this object and remain unmoved. Moving the campaign preserves state
// addresses and weak-support identities. Move-assignment invalidates references
// previously returned from the target. Read spans remain valid until the next
// funding mutation or destruction/replacement of this campaign.
class AdaptiveResearchCampaignState final {
public:
  ~AdaptiveResearchCampaignState();
  AdaptiveResearchCampaignState(AdaptiveResearchCampaignState &&) noexcept;
  AdaptiveResearchCampaignState &
  operator=(AdaptiveResearchCampaignState &&) noexcept;
  AdaptiveResearchCampaignState(const AdaptiveResearchCampaignState &) = delete;
  AdaptiveResearchCampaignState &
  operator=(const AdaptiveResearchCampaignState &) = delete;

  [[nodiscard]] const AdaptiveResearchStrategicRuntime &runtime() const noexcept;
  [[nodiscard]] std::span<const int> civilization_ids() const noexcept;
  [[nodiscard]] const AdaptiveResearchCivilizationState *
  try_get_civilization(int civilization_id) const noexcept;
  [[nodiscard]] const AdaptiveResearchCivilizationState &
  get_civilization(int civilization_id) const;
  [[nodiscard]] const AdaptiveResearchCivilizationStart *
  try_get_start(int civilization_id) const noexcept;
  [[nodiscard]] const AdaptiveResearchCivilizationStart &
  get_start(int civilization_id) const;
  [[nodiscard]] std::span<const AdaptiveResearchProjectFundingState>
  project_funding(int civilization_id) const;
  [[nodiscard]] const AdaptiveResearchPlan &plan(int civilization_id) const;
  [[nodiscard]] AdaptiveResearchCommandResult edit_plan(
      int civilization_id, ResearchPlanCommand command,
      std::string_view node_id = {});

private:
  struct Storage;
  explicit AdaptiveResearchCampaignState(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;

  friend class AdaptiveResearchCampaignFactory;
  friend class AdaptiveResearchCampaignSnapshotCodec;
  friend class detail::AdaptiveResearchCampaignStateAccess;
};

// Borrows one stable strategic runtime. Returned campaign state owns all
// civilization-specific values; it continues to borrow the same runtime.
class AdaptiveResearchCampaignFactory final {
public:
  static constexpr std::string_view terran_profile_id =
      "reference_humanlike_solar_2050";
  static constexpr std::string_view pelagic_profile_id =
      "reference_pelagic_high_pressure_early_space";
  static constexpr std::string_view high_gravity_profile_id =
      "reference_high_gravity_metabolic_early_space";
  static constexpr std::string_view cryogenic_hydrocarbon_profile_id =
      "reference_cryogenic_hydrocarbon_early_space";

  explicit AdaptiveResearchCampaignFactory(
      const AdaptiveResearchStrategicRuntime &runtime) noexcept;
  AdaptiveResearchCampaignFactory(AdaptiveResearchStrategicRuntime &&) = delete;

  [[nodiscard]] AdaptiveResearchCampaignState
  create(std::span<const Civilization> civilizations) const;
  [[nodiscard]] AdaptiveResearchCampaignState
  create(const FreshCampaignState &campaign) const;
  [[nodiscard]] static std::string_view
  select_reference_profile(std::string_view species_id);

private:
  const AdaptiveResearchStrategicRuntime *runtime_{};
};

// Borrows one stable strategic runtime and composes its schema-5 codec in final
// storage. Snapshot DTOs own their values. Restore consumes no caller-owned
// values and returns a campaign that borrows the same runtime.
class AdaptiveResearchCampaignSnapshotCodec final {
public:
  static constexpr int current_schema_version = 3;

  explicit AdaptiveResearchCampaignSnapshotCodec(
      const AdaptiveResearchStrategicRuntime &runtime);
  AdaptiveResearchCampaignSnapshotCodec(AdaptiveResearchStrategicRuntime &&) =
      delete;
  ~AdaptiveResearchCampaignSnapshotCodec();
  AdaptiveResearchCampaignSnapshotCodec(
      AdaptiveResearchCampaignSnapshotCodec &&) noexcept;
  AdaptiveResearchCampaignSnapshotCodec &
  operator=(AdaptiveResearchCampaignSnapshotCodec &&) noexcept;
  AdaptiveResearchCampaignSnapshotCodec(
      const AdaptiveResearchCampaignSnapshotCodec &) = delete;
  AdaptiveResearchCampaignSnapshotCodec &
  operator=(const AdaptiveResearchCampaignSnapshotCodec &) = delete;

  [[nodiscard]] AdaptiveResearchCampaignSnapshot
  capture(const AdaptiveResearchCampaignState &campaign) const;
  [[nodiscard]] AdaptiveResearchCampaignState
  restore(std::span<const Civilization> civilizations,
          const AdaptiveResearchCampaignSnapshot &snapshot) const;
  [[nodiscard]] AdaptiveResearchCampaignState
  restore(const FreshCampaignState &campaign,
          const AdaptiveResearchCampaignSnapshot &snapshot) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
