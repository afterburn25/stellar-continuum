#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <stellar/engine/resource_amount.hpp>

namespace stellar::engine {

class Inventory;

// Colony/settlement structural framework — the reusable substrate a
// settlement is built from: districts host structures, structures
// produce jobs/housing/production and draw utilities. Concrete content
// (power plant, hab block, mine) is data — DistrictSpec/StructureSpec
// rows — not code. Designed to sit behind SimulationExecutor tiers and
// beside Population (which supplies workers/headcount) and
// ResourceNetwork (which holds stockpiles/transfers): the colony turns
// population + stockpiles into jobs, housing and production.
//
// Deterministic by construction: expected-value double math, structures
// and districts processed in ascending instance-id order, no RNG.
// Instance ids are CALLER-SUPPLIED so a game's save identity maps
// directly onto framework objects.

// ResourceAmount is the shared {resource id, qty} record from
// resource_amount.hpp (aggregate-init compatible with the {id, qty}
// literals used throughout).


// Static district template. A district is a developed parcel that hosts
// a bounded number of structures (its slots) and draws utilities itself.
struct DistrictSpec {
    std::string id;             // "district.industrial"
    std::string category;       // free query tag
    std::uint32_t structure_slots{4};
    std::vector<ResourceAmount> build_cost;
    double build_days{0.0};
    std::vector<ResourceAmount> utility_demand_per_day;
    std::vector<ResourceAmount> upkeep_per_day;
    std::vector<std::string> required_tags; // environment/tech tags the site needs
};

// Static structure template. A structure either occupies a district slot
// (`district` = required DistrictSpec id) or stands alone ("" = built
// directly on the settlement — still bounded by standalone_slots on the
// colony).
struct StructureSpec {
    std::string id;             // "structure.fusion_plant"
    std::string category;
    std::string district;       // required district spec, "" = standalone
    std::vector<ResourceAmount> build_cost;
    double build_days{0.0};
    std::vector<ResourceAmount> utility_demand_per_day; // "power": 5
    std::vector<ResourceAmount> utility_supply_per_day; // power plant supplies
    std::vector<ResourceAmount> upkeep_per_day;         // drawn while complete
    std::vector<ResourceAmount> inputs_per_day;         // consumed while operating
    std::vector<ResourceAmount> outputs_per_day;        // produced while operating
    double jobs{0.0};            // workforce positions while operating
    double housing{0.0};         // population capacity provided
    double condition_decay_per_day{0.0}; // wear at zero maintenance
    double condition_repair_per_day{0.0}; // restoration at full maintenance
    std::vector<std::string> required_tags;
};

// Runtime instances. `construction_remaining` counts down in advance();
// a structure/district only contributes once `complete`. `operating`
// records the last advance's satisfaction ratio (utility × workforce ×
// input supply) — 1.0 means fully productive.
struct District {
    std::uint64_t id{};
    std::string spec_id;
    double construction_remaining{0.0};
    bool complete{false};
    bool enabled{true};
};

struct Structure {
    std::uint64_t id{};
    std::string spec_id;
    std::uint64_t district_id{0}; // 0 = standalone
    double construction_remaining{0.0};
    bool complete{false};
    bool enabled{true};
    double condition{1.0};
    double operating{1.0};
};

// Inputs the settlement consumes for one advance step. `stockpile` is
// optional: when present, upkeep/inputs are drawn and outputs deposited;
// when absent the delta reports unmet demand and gross output so the
// owner can settle accounts itself.
struct ColonyInputs {
    double workers_available{0.0};
    Inventory* stockpile{nullptr};
    double maintenance{1.0}; // 0..1 upkeep funding coverage
    // Site/environment tags available for `required_tags` matching
    // (e.g. "site.volcanic", "tech.fusion"). Unmet requirements block
    // construction starts, not operation of existing builds.
    std::vector<std::string> available_tags;
};

struct ColonyDelta {
    double elapsed_days{0.0};
    double jobs_total{0.0};
    double jobs_filled{0.0};
    double housing_capacity{0.0};
    std::vector<ResourceAmount> outputs_produced;   // net after stockpile
    std::vector<ResourceAmount> upkeep_shortfall;   // demanded, unmet
    std::vector<ResourceAmount> input_shortfall;
    // utility id -> {supply, demand} aggregate for the step
    std::vector<std::pair<std::string, std::pair<double, double>>> utilities;
    std::uint32_t structures_completed{0};
    std::uint32_t districts_completed{0};
};

// A settlement: districts + structures + utility balance. Holds no
// population itself — pair with Population for demographics and with an
// EconomyNode/Inventory for stockpiles.
class Colony {
public:
    void define_district(DistrictSpec spec);   // duplicate id throws
    void define_structure(StructureSpec spec); // duplicate id or unknown
                                               // district ref throws
    [[nodiscard]] const DistrictSpec* district_spec(std::string_view id) const;
    [[nodiscard]] const StructureSpec* structure_spec(std::string_view id) const;

    // Capacity bounds for standalone (district-less) structures.
    void set_standalone_slots(std::uint32_t slots) { standalone_slots_ = slots; }
    [[nodiscard]] std::uint32_t standalone_slots() const { return standalone_slots_; }

    // Construction. Caller pays build_cost (see spec) before calling;
    // `id` must be unused. Returns false on unknown spec, missing/failed
    // requirement tags, missing district, or full district/standalone
    // slots. Construction completes inside advance().
    bool build_district(std::uint64_t id, std::string_view spec_id,
                        const std::vector<std::string>& available_tags = {});
    bool build_structure(std::uint64_t id, std::string_view spec_id,
                         std::uint64_t district_id = 0,
                         const std::vector<std::string>& available_tags = {});
    bool demolish_district(std::uint64_t id);  // fails while it hosts structures
    bool demolish_structure(std::uint64_t id);
    bool set_enabled(std::uint64_t structure_id, bool enabled);
    bool set_district_enabled(std::uint64_t district_id, bool enabled);

    [[nodiscard]] const District* district(std::uint64_t id) const;
    [[nodiscard]] const Structure* structure(std::uint64_t id) const;
    [[nodiscard]] std::vector<const District*> districts() const;   // by id
    [[nodiscard]] std::vector<const Structure*> structures() const; // by id
    [[nodiscard]] std::vector<const Structure*> structures_in(std::uint64_t district_id) const;
    [[nodiscard]] std::size_t structure_count() const { return structures_.size(); }

    // Aggregate capacities over complete+enabled structures.
    [[nodiscard]] double jobs_total() const;
    [[nodiscard]] double housing_capacity() const;
    // utility id -> {supply per day, demand per day} over complete+enabled
    // districts and structures, independent of advance().
    [[nodiscard]] std::vector<std::pair<std::string, std::pair<double, double>>>
    utility_balance() const;

    // Advance construction, draw upkeep/inputs, produce outputs, update
    // condition and per-structure operating ratios. Deterministic: fixed
    // ascending-id processing, expected-value math.
    ColonyDelta advance(double elapsed_days, const ColonyInputs& inputs);

    // --- persistence -------------------------------------------------
    // Serializable colony state: district/structure instances with their
    // construction/condition bookkeeping. DistrictSpec/StructureSpec are
    // definitions — re-registered on load like recipes.
    struct DistrictState {
        std::uint64_t id{};
        std::string spec_id;
        double construction_remaining{0.0};
        bool complete{false};
        bool enabled{true};
    };
    struct StructureState {
        std::uint64_t id{};
        std::string spec_id;
        std::uint64_t district_id{0};
        double construction_remaining{0.0};
        bool complete{false};
        bool enabled{true};
        double condition{1.0};
        double operating{1.0};
    };
    struct State {
        std::uint32_t version{1};
        std::uint32_t standalone_slots{0};
        std::vector<DistrictState> districts;   // sorted by id
        std::vector<StructureState> structures; // sorted by id
    };
    [[nodiscard]] State capture_state() const;
    // Replaces all instances with the snapshot. Throws invalid_argument
    // on unknown spec ids (content mismatch) or a structure whose
    // district_id is not present in the snapshot.
    void restore_state(const State& state);

private:
    std::unordered_map<std::string, DistrictSpec> district_specs_;
    std::unordered_map<std::string, StructureSpec> structure_specs_;
    std::unordered_map<std::uint64_t, District> districts_;
    std::unordered_map<std::uint64_t, Structure> structures_;
    std::uint32_t standalone_slots_{0}; // 0 = unlimited standalone builds
};

} // namespace stellar::engine
