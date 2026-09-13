#pragma once

#include <stellar/core/fleet_state.hpp>

#include <span>

namespace stellar::core {
inline constexpr float fleet_local_gate_radius = 0.82F;

bool finite_fleet_chart_position(Vec2 value);
double fleet_local_transit_rate(const FleetState &fleet);
Vec2 fleet_gate_towards(Vec2 source_system_position,
                        Vec2 current_system_position);
void begin_fleet_local_transit(FleetState &fleet, FleetTransitPhase phase,
                               Vec2 start, Vec2 target);
double advance_fleet_local_transit(FleetState &fleet, double available_days);
bool fleet_local_transit_complete(const FleetState &fleet);
double fleet_local_transit_remaining_days(const FleetState &fleet);
double fleet_remaining_chart_distance(std::span<const StellarSystem> systems,
                                      const FleetState &fleet);

Vec2 interpolate_chart_position(const StellarSystem &origin,
                                const StellarSystem &target, double progress);
double interstellar_distance_from_fleet(std::span<const StellarSystem> systems,
                                        const FleetState &fleet,
                                        const StellarSystem &target);
} // namespace stellar::core
