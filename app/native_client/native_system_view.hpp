#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/knowledge.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/planetary_classification.hpp>
#include <stellar/core/planetary_satellites.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_system {

enum class NativeSystemBodyVisualClass {
  unknown_planet, unknown_moon, rocky, oceanic, frozen, hot_rocky,
  gas_giant, ice_giant, moon
};
enum class NativePositiveSignature { rare_resource, anomaly, activity };

struct NativeSystemBodyDetails {
  double mass_earth{}, gravity_g{}, temperature_kelvin{}, pressure_kpa{};
  stellar::core::PlanetaryAtmosphereRegime atmosphere{};
  stellar::core::PlanetarySolventRegime available_solvent{};
  double radiation_hazard{};
  bool is_immersed_environment{}, has_solid_surface{};
  std::optional<stellar::core::StellarPlanetProperties> stellar_exposure;
};

struct NativeSystemBody {
  int id{};
  std::optional<int> parent_body_id;
  int orbit_index{};
  std::string name;
  stellar::core::PlanetaryBodyKind kind{};
  double radius_earth{};
  double orbital_eccentricity{}, orbital_inclination_degrees{};
  std::vector<NativePositiveSignature> positive_signatures;
  std::optional<NativeSystemBodyDetails> details;
  NativeSystemBodyVisualClass visual_class{NativeSystemBodyVisualClass::unknown_planet};
  std::optional<stellar::core::PlanetaryWorldClass> world_class;
  // Present only for fully surveyed canonical Sol bodies with an approved asset.
  std::optional<std::string> sol_texture_key;
  double orbit_au{};
  std::optional<stellar::core::PlanetAppearance> appearance;
  int stellar_host{};
  std::optional<stellar::engine::AnalyticOrbit> stellar_orbit;
  std::optional<stellar::core::SatelliteOrbit> satellite_orbit;
};

struct NativeSystemSnapshot {
  std::uint64_t campaign_generation{};
  int observer_civilization_id{}, system_id{};
  std::string catalog_name;
  stellar::core::SystemSurveyLevel survey_level{};
  double survey_progress{};
  std::optional<stellar::core::StarArchetype> archetype;
  std::optional<stellar::core::StellarClass> primary_stellar_class,
      secondary_stellar_class, tertiary_stellar_class;
  std::vector<NativeSystemBody> bodies;
  std::optional<stellar::core::StellarPhysicalProperties> stellar_object;
  std::vector<stellar::core::SmallBodyField> small_body_fields;
  double simulation_days{};
  bool developer{};
  std::optional<stellar::core::StellarOrbitArchitecture> stellar_orbits;
};

struct NativeSystemViewResult {
  std::optional<NativeSystemSnapshot> snapshot;
  std::string denial;
};

class NativeSystemViewController final {
public:
  [[nodiscard]] NativeSystemViewResult
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
        int selected_system_id);
  [[nodiscard]] bool is_current_generation(std::uint64_t) const noexcept;
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
private:
  void require_owner() const;
  void bind_generation(std::uint64_t);
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  const stellar::engine::LocalizationTable *locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
};

struct SystemSpatialPoint { float x{}, y{}, height{}; };
struct SystemSpatialBodyMarker {
  int body_id{};
  std::optional<int> parent_body_id;
  int orbit_index{};
  std::string label;
  stellar::core::PlanetaryBodyKind kind{};
  NativeSystemBodyVisualClass visual_class{};
  float offset_x{}, offset_y{}, offset_height{}, orbit_radius{}, display_radius{};
  double orbital_eccentricity{}, orbital_inclination_degrees{};
  std::vector<NativePositiveSignature> positive_signatures;
  std::optional<std::string> sol_texture_key;
  double physical_orbit_au{};
  int stellar_host{};
  std::optional<stellar::core::SatelliteOrbit> satellite_orbit;
  std::optional<stellar::engine::AnalyticOrbit> stellar_orbit;
};
struct SystemSpatialSnapshot {
  int system_id{};
  float design_radius{};
  std::vector<SystemSpatialBodyMarker> bodies;
  std::vector<std::pair<double,float>> orbit_anchors;
  std::array<SystemSpatialPoint,4> stellar_hosts{};
  std::array<float,2> stellar_orbit_scales{};
  std::array<std::vector<SystemSpatialPoint>,3> stellar_paths;
  int belt_host{};
};
[[nodiscard]] float system_display_orbit_radius(const SystemSpatialSnapshot&,double au);
[[nodiscard]] SystemSpatialPoint projected_orbit_point(const SystemSpatialSnapshot&,double au,float eccentricity,float inclination,float anomaly);
[[nodiscard]] std::vector<SystemSpatialPoint> projected_orbit_path(const SystemSpatialSnapshot&,const SystemSpatialBodyMarker&);

[[nodiscard]] float body_display_radius(double, stellar::core::PlanetaryBodyKind);
// The chart keeps zooming for planets; stellar sprites stop at source detail.
[[nodiscard]] float star_screen_radius(float scale,float visual_scale=1.f);
[[nodiscard]] SystemSpatialPoint orbit_point(float radius, float eccentricity,
                                               float inclination_degrees,
                                               float eccentric_anomaly);
[[nodiscard]] std::vector<SystemSpatialPoint>
orbit_path(float radius, float eccentricity, float inclination_degrees);
void update_system_motion(const NativeSystemSnapshot&,SystemSpatialSnapshot&);
[[nodiscard]] stellar::core::StellarSystem snapshot_stellar_system(const NativeSystemSnapshot&);
[[nodiscard]] SystemSpatialSnapshot project_system(const NativeSystemSnapshot &);

double parent_facing_bearing(const SystemSpatialSnapshot&,const SystemSpatialBodyMarker&);

struct SystemSpatialViewport {
  float center_x{}, center_y{}, scale{1};
  [[nodiscard]] static SystemSpatialViewport fit(const SystemSpatialSnapshot &,
                                                  float width, float height);
  [[nodiscard]] SystemSpatialPoint world_to_screen(float x, float y) const;
  [[nodiscard]] SystemSpatialPoint screen_to_world(float x, float y) const;
  [[nodiscard]] float body_radius(const SystemSpatialBodyMarker &) const;
  [[nodiscard]] bool is_body_visible(const SystemSpatialSnapshot &,
                                     const SystemSpatialBodyMarker &) const;
  [[nodiscard]] std::optional<int> hit_body(const SystemSpatialSnapshot &,
                                             float x, float y) const;
  [[nodiscard]] bool hits_star(float x, float y) const;
  [[nodiscard]] SystemSpatialViewport translated(float dx, float dy) const;
  [[nodiscard]] SystemSpatialViewport zoomed_at(float factor, float pointer_x,
                                                 float pointer_y,
                                                 float minimum_scale,
                                                 float maximum_scale) const;
};

} // namespace stellar::native_system
