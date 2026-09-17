#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace stellar::native_surface_building {

inline constexpr std::size_t maximum_geometry_triangles = 8192;
inline constexpr std::size_t maximum_type_id_bytes = 256;

struct Vec3 {
  float x{}, y{}, z{};

  bool operator==(const Vec3 &) const = default;
};

struct Material {
  float r{}, g{}, b{};
  bool emissive{};
};

struct Triangle {
  Vec3 a{}, b{}, c{};
  Material material{};
};

struct Bounds3 {
  Vec3 minimum{}, maximum{};
};

// Authoritative immutable input captured by a later preparation job. A
// positive hub_level selects hub geometry; otherwise type_id selects a
// building family.
struct SurfaceBuildingState {
  std::string type_id;
  float rotation_degrees{};
  bool complete{};
  double progress_fraction{};
  bool powered{};
  bool enabled{};
  bool staffed{};
  double condition{1};
  int hub_level{};
  bool capital{};
  bool outpost{};
};

enum class ConstructionVisualStage : std::uint8_t {
  Foundation,
  Structure,
  Fitted,
};

enum class ConditionVisualState : std::uint8_t {
  Normal,
  Repair,
  Critical,
};

// Cheap, normalized cache identity. Continuously changing progress and
// condition values become only the finite visual states used by geometry.
struct SurfaceBuildingStateKey {
  std::string type_id;
  float rotation_degrees{};
  bool complete{};
  ConstructionVisualStage construction_stage{};
  bool powered{};
  bool enabled{};
  bool staffed{};
  ConditionVisualState condition_state{};
  int hub_level{};
  bool capital{};
  bool outpost{};

  bool operator==(const SurfaceBuildingStateKey &) const = default;
};

struct SurfaceBuildingGeometry {
  SurfaceBuildingStateKey state;
  std::vector<Triangle> triangles;
  Vec3 ground_anchor{};
  Bounds3 model_bounds{};
  float canonical_footprint_radius{};
  bool has_scaffolding{};
};

enum class SurfaceBuildingGeometryError {
  None,
  InvalidState,
  GeometryBudgetExceeded,
  InvalidGeometry,
};

struct SurfaceBuildingGeometryResult {
  std::shared_ptr<const SurfaceBuildingGeometry> geometry;
  SurfaceBuildingGeometryError error{SurfaceBuildingGeometryError::None};
  std::string message;
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(geometry);
  }
};

struct SurfaceBuildingStateResult {
  std::optional<SurfaceBuildingStateKey> key;
  SurfaceBuildingGeometryError error{SurfaceBuildingGeometryError::None};
  std::string message;
  [[nodiscard]] explicit operator bool() const noexcept {
    return key.has_value();
  }
};

[[nodiscard]] SurfaceBuildingStateResult
normalize_surface_building_state(const SurfaceBuildingState &state);

[[nodiscard]] SurfaceBuildingGeometryResult
prepare_geometry(const SurfaceBuildingStateKey &state);

[[nodiscard]] SurfaceBuildingGeometryResult
prepare_geometry(const SurfaceBuildingState &state);

} // namespace stellar::native_surface_building
