#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/knowledge.hpp>
#include <stellar/core/planetary_catalog.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
  // Present only for fully surveyed canonical Sol bodies with an approved asset.
  std::optional<std::string> sol_texture_key;
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
private:
  void require_owner() const;
  void bind_generation(std::uint64_t);
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
};
struct SystemSpatialSnapshot {
  int system_id{};
  float design_radius{};
  std::vector<SystemSpatialBodyMarker> bodies;
};

[[nodiscard]] float body_display_radius(double, stellar::core::PlanetaryBodyKind);
[[nodiscard]] float star_screen_radius(float scale);
[[nodiscard]] SystemSpatialPoint orbit_point(float radius, float eccentricity,
                                               float inclination_degrees,
                                               float eccentric_anomaly);
[[nodiscard]] std::vector<SystemSpatialPoint>
orbit_path(float radius, float eccentricity, float inclination_degrees);
[[nodiscard]] SystemSpatialSnapshot project_system(const NativeSystemSnapshot &);

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
