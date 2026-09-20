#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/stellar_object.hpp>

namespace stellar::core {

struct DeveloperGiantTestRequest {
  PlanetClass type{PlanetClass::GasGiant};std::string subclass{"cream-band"};
  int planet_variant{},ring_variant{};bool rings{true};std::string ring_family{"dense-banded-ring"};
  double axial_tilt_degrees{27},distance_scale{1};
  StellarObjectType star_type{StellarObjectType::GYellowStar};
  bool operator==(const DeveloperGiantTestRequest&)const=default;
};
struct DeveloperGiantTestState {
  DeveloperGiantTestRequest controls;
  PlanetAppearance appearance;
  bool operator==(const DeveloperGiantTestState&)const=default;
};

struct DeveloperSimulationState {
  bool fixed_ticks{};
  std::uint32_t speed{1};
  std::uint64_t completed_ticks{};
  std::int64_t backlog_nanoseconds{};
  std::uint64_t tactical_completed_ticks{};
  std::int64_t tactical_backlog_nanoseconds{};
  bool operator==(const DeveloperSimulationState &) const = default;
};

struct CampaignDeveloperProvenance {
  bool tools_used{};
  bool normal_research_completed{};
  bool special_research_completed{};
  DeveloperSimulationState simulation;
  bool player_ai_control{};
  bool full_celestial_coverage{};
  std::string coverage_generation_version;
  std::vector<int> coverage_forced_system_ids;
  bool full_exploration{};
  // Isolated, authoritative test fixture; never changes the generated galaxy.
  std::optional<DeveloperGiantTestState> giant_test;

  bool operator==(const CampaignDeveloperProvenance &) const = default;
};

} // namespace stellar::core
