#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Strategic warfare substrate — fleets as cohort aggregates, not
// per-ship entities. A ShipCohort is (class × count × condition ×
// experience); a Fleet is a cohort set at a strategic position with an
// order. Combat resolution is Lanchester-style aggregate attrition over
// elapsed days — deterministic expected values, no per-ship rolls.
// Designed for executor tiers: fleets between engagements advance on
// slow cadences, engagements run hot.
//
// Interdiction, not presence: an Interdict order projects a zone that
// gates warp for hostile fleets; merely co-located fleets do not block
// movement. Engagement is explicit — the caller starts it, the engine
// resolves attrition deterministically.

// Static ship-class template — data-driven.
struct ShipClass {
    std::string id;              // "class.destroyer"
    std::string role;            // "line", "escort", "carrier", "transport"
    double attack{0.0};          // damage/day vs hull
    double defense{0.0};         // flat damage reduction per ship
    double hull{1.0};            // hull points per ship
    double speed{1.0};           // strategic units/day (fleet = min)
    double supply_per_day{0.0};  // upkeep draw while active
    double interdiction{0.0};    // radius contributed per ship
};

// Aggregate of identical ships — the scale trick: 10,000 destroyers are
// one cohort, not 10,000 entities.
struct ShipCohort {
    std::string ship_class;
    double count{0.0};
    double condition{1.0};   // mean readiness 0..1, scales attack/hull
    double experience{0.0};  // 0..1, scales attack
};

enum class FleetOrderKind : std::uint8_t {
    Hold,
    Move,      // toward target_x/target_y at fleet speed
    Interdict, // hold position; project interdiction zone
    Retreat    // disengaged; caller routes it away
};

struct FleetOrder {
    FleetOrderKind kind{FleetOrderKind::Hold};
    double target_x{0.0};
    double target_y{0.0};
};

struct FleetState {
    std::uint64_t id{};
    std::uint64_t owner{0};        // faction id — caller-owned
    double x{0.0}, y{0.0};         // strategic-plane position
    FleetOrder order;
    bool engaged{false};           // inside a resolve_engagement call
};

struct FleetReport {
    double ships{0.0};
    double attack{0.0};            // effective dps aggregate
    double hull{0.0};              // effective hull pool
    double speed{0.0};             // slowest cohort
    double interdiction_radius{0.0};
    double supply_per_day{0.0};
};

struct EngagementResult {
    double elapsed_days{0.0};
    double a_ships_lost{0.0};
    double b_ships_lost{0.0};
    bool a_destroyed{false};
    bool b_destroyed{false};
};

// Catalog + fleet collection + engagement resolution. Caller owns
// faction identity — hostility is the caller's contract (resolve()
// takes the two fleets explicitly).
class WarfareModel {
public:
    void define_class(ShipClass cls); // duplicate id throws
    [[nodiscard]] const ShipClass* ship_class(std::string_view id) const;

    bool add_fleet(std::uint64_t id, std::uint64_t owner,
                   double x, double y);
    bool remove_fleet(std::uint64_t id);
    // Adds ships (merges cohort on same class); class must be defined.
    bool add_ships(std::uint64_t fleet_id, std::string_view class_id,
                   double count, double condition = 1.0,
                   double experience = 0.0);
    bool remove_ships(std::uint64_t fleet_id, std::string_view class_id,
                      double count);
    bool set_order(std::uint64_t fleet_id, FleetOrder order);
    bool set_position(std::uint64_t fleet_id, double x, double y);

    [[nodiscard]] const FleetState* fleet(std::uint64_t id) const;
    [[nodiscard]] const ShipCohort* cohort(std::uint64_t fleet_id,
                                           std::string_view class_id) const;
    [[nodiscard]] std::vector<const FleetState*> fleets() const; // by id
    [[nodiscard]] std::vector<const ShipCohort*> cohorts(
        std::uint64_t fleet_id) const; // by class id

    // Aggregate strength/speed/supply of a fleet.
    [[nodiscard]] FleetReport report(std::uint64_t fleet_id) const;

    // Interdiction: is a point covered by a fleet holding an Interdict
    // order belonging to a DIFFERENT owner than `mover_owner`? Same-
    // owner interdictors never gate their own side's movement.
    // `except_fleet` lets a mover skip itself.
    [[nodiscard]] bool interdicted(double x, double y,
                                   std::uint64_t mover_owner,
                                   std::uint64_t except_fleet = 0) const;
    [[nodiscard]] double interdiction_radius(std::uint64_t fleet_id) const;

    // Movement + supply burn for elapsed days. Move orders approach
    // their target at fleet speed (slowest cohort × condition); arrival
    // clamps to target. Deterministic.
    void advance(double elapsed_days);

    // Deterministic Lanchester attrition between two fleets over
    // elapsed days: each side's aggregate attack applies to the
    // other's hull pool, distributed across cohorts by hull share,
    // net of per-ship defense. Marks fleets engaged; clears on
    // destruction. Caller decides retreat (set order Retreat and stop
    // calling resolve).
    EngagementResult resolve(std::uint64_t a, std::uint64_t b,
                             double elapsed_days);

    // --- persistence -------------------------------------------------
    // Serializable warfare state: fleets (position, orders, engaged
    // flag) and their ship cohorts (count/condition/experience).
    // ShipClass rows are definitions — re-registered on load.
    struct FleetEntry {
        FleetState state;
        std::vector<ShipCohort> cohorts; // sorted by ship_class
    };
    struct State {
        std::uint32_t version{1};
        std::vector<FleetEntry> fleets; // sorted by fleet id
    };
    [[nodiscard]] State capture_state() const;
    // Replaces all fleets with the snapshot. Throws invalid_argument on
    // a cohort referencing an undefined ship class.
    void restore_state(const State& state);

private:
    std::unordered_map<std::string, ShipClass> classes_;
    std::unordered_map<std::uint64_t, FleetState> fleets_;
    std::unordered_map<std::uint64_t, std::vector<ShipCohort>> cohorts_;
};

} // namespace stellar::engine
