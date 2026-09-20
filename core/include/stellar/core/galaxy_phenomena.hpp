#pragma once
#include <stellar/core/galaxy_configuration.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/engine/organic_region.hpp>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {
inline constexpr std::string_view galaxy_phenomena_version="galaxy-phenomena-v2";
enum class PhenomenonType { Emission, Reflection, DarkNebula, MolecularCloud, HII,
  SupernovaRemnant, Radiation, IonizedGas, DustLane, Exotic,
  DiffuseGas, MixedNebula, RareEnergetic, Count };
inline constexpr std::size_t phenomenon_type_count=static_cast<std::size_t>(PhenomenonType::Count);
struct PhenomenonEffects {
  double sensor{1}, scanning{1}, movement{1}, hazard{}, colonization{}, research{},
    anomaly_bias{}, resource_bias{}, concealment{}, combat_visibility{1}, attrition{}, interference{};
  bool operator==(const PhenomenonEffects&)const=default;
};
struct PhenomenonDefinition {
  std::string id,name,description,rarity;
  stellar::engine::OrganicShape shape{};
  std::array<int,3> color{};
  std::array<double,2> extent{}, intensity{};
  double weight{},opacity{},elongation{1};
  bool star_forming{};
  PhenomenonEffects effects;
};
struct GalaxyPhenomenon {
  std::uint32_t id{};
  PhenomenonType type{};
  std::string designation;
  stellar::engine::OrganicRegion shape;
  double intensity{},opacity{};
  std::array<int,3> color{};
  StellarRegion affinity{};
  PhenomenonEffects effects;
  std::vector<int> systems_contained;
  StellarDiscoveryHooks hooks;
  // Empty only in v1 saves. Rendering resolves those through the frozen v1 art catalog.
  std::string asset_id;
  bool mirrored{};
  double natural_weight{},population_modifier{1},morphology_modifier{1};
  bool operator==(const GalaxyPhenomenon&)const=default;
};
struct GalaxyPhenomena {
  std::string version{galaxy_phenomena_version},configuration_fingerprint;
  std::vector<GalaxyPhenomenon> regions;
  bool operator==(const GalaxyPhenomena&)const=default;
};
struct PhenomenonOverlap {
  std::uint32_t id{};
  double overlap{},density{},intensity{},edge_distance{},center_distance{};
};
struct SystemPhenomenonContext {
  std::vector<PhenomenonOverlap> overlaps;
  std::optional<std::uint32_t> dominant;
  std::uint64_t local_seed{};
  double visual_intensity{};
  double world_x{},world_y{};
  PhenomenonEffects effects;
};
const std::array<PhenomenonDefinition,phenomenon_type_count>& phenomenon_definitions();
struct PhenomenonWeight {double base{},population{},morphology{},normalized{};};
std::array<PhenomenonWeight,phenomenon_type_count> phenomenon_weights(GalaxyMorphology,PopulationState);
int natural_phenomenon_count(int system_count,GalaxyMorphology,PopulationState,std::uint64_t seed);
const PhenomenonDefinition& phenomenon_definition(PhenomenonType);
GalaxyPhenomena generate_galaxy_phenomena(const GalaxyGenerationConfig&,std::span<const StellarSystem>);
void validate_galaxy_phenomena(const GalaxyPhenomena&,const GalaxyGenerationConfig&,std::span<const StellarSystem>);
SystemPhenomenonContext phenomenon_context(const GalaxyPhenomena*,double x,double y,int system_id=0);
std::optional<PhenomenonOverlap> nearest_phenomenon(const GalaxyPhenomena*,double x,double y);
double phenomenon_footprint_density(const GalaxyGenerationConfig&,double x,double y);
std::string phenomena_diagnostics(const GalaxyPhenomena*,const SystemPhenomenonContext* = nullptr);
} // namespace stellar::core
