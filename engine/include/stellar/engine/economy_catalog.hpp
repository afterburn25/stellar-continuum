#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <stellar/engine/resource_amount.hpp>
#include <stellar/engine/resource_economy.hpp>

namespace stellar::engine {

// Economy catalog — the data-driven definition layer for strategic
// economies, layered over the runtime ResourceNetwork
// (resource_economy.hpp). Content authors describe ResourceSpec rows and
// RecipeSpec rows; EconomyCatalog stores them with strict validation,
// EconomyGraph exposes the production dependency graph, and
// analyze_economy produces demand/bottleneck diagnostics. Concrete game
// rules (which resources exist, what recipes cost) live in data packages
// and Core — never hardcoded here.

enum class ResourceCategory : std::uint8_t {
    Raw,         // mined/extracted: ore, ice, volatiles
    Refined,     // processed materials: steel, propellant
    Component,   // manufactured parts: hull plate, machinery
    Consumable,  // food, water, life-support consumables
    Energy,      // power/fuel class resources
    Labor,       // workforce capacity
    Information, // data, research output
    Abstract,    // nonphysical strategic counters (influence, credits)
};

enum class StorageClass : std::uint8_t {
    None,       // nonphysical / flow-only
    Bulk,       // solids: ore, metal, plate
    Liquid,     // water, propellant
    Gas,        // hydrogen, oxygen, atmosphere feeds
    Cryo,       // liquefied volatiles
    Contained,  // hazardous/delicate goods
    Live,       // organisms, crew, livestock
};

struct ResourceSpec {
    std::string id;                  // stable content id, e.g. "res.steel"
    std::string name_key;            // localization key
    ResourceCategory category{ResourceCategory::Raw};
    bool physical{true};             // false: energy/data/labor flows
    double mass_per_unit{0.0};       // tonnes per unit (physical: must be > 0)
    double volume_per_unit{0.0};     // cubic metres per unit (0 = negligible)
    StorageClass storage{StorageClass::Bulk};
    double decay_per_day{0.0};       // perishability fraction/day; 0 = stable
    bool transportable{true};        // can move on logistics lanes
    double base_value{1.0};          // valuation metadata for pricing/AI
    std::string substitution_group;  // inputs may substitute within a group
    std::vector<std::string> tags;   // strategic tags: "strategic", "volatile", ...
    std::string unit;                // informational unit label: "t", "MWh", ...
};

struct RecipeSpec {
    std::string id;
    std::string name_key;
    std::vector<ResourceAmount> inputs;     // consumed per run
    std::vector<ResourceAmount> outputs;    // primary products per run
    std::vector<ResourceAmount> catalysts;  // required present, not consumed
    std::vector<ResourceAmount> byproducts; // always-produced secondary output
    double labor_persons{0.0};              // workforce requirement per run
    std::string labor_skill;                // occupation tag, "" = unskilled
    double energy_units{0.0};               // energy consumed per run
    std::string energy_resource;            // "" = use catalog energy resource
    std::vector<std::string> facility_tags; // required facility capabilities
    double duration_days{1.0};
    double efficiency{1.0};                 // output/byproduct multiplier
    bool allow_substitution{false};         // inputs may swap within groups
};

enum class ValidationSeverity : std::uint8_t { Error, Warning };

struct ValidationIssue {
    ValidationSeverity severity{ValidationSeverity::Error};
    std::string record; // resource/recipe id, or the catalog name
    std::string field;
    std::string reason;
};

// Dependency graph over the catalog: resource nodes connected through
// recipes. Built once per catalog generation; immutable.
class EconomyGraph {
public:
    struct Edge {
        std::string recipe;
        std::string from; // input resource
        std::string to;   // output resource
    };

    // Recipes producing / consuming (inputs + catalysts) a resource.
    [[nodiscard]] std::vector<std::string> producers_of(std::string_view resource) const;
    [[nodiscard]] std::vector<std::string> consumers_of(std::string_view resource) const;
    // Transitive closures, sorted for determinism.
    [[nodiscard]] std::vector<std::string> downstream_resources(std::string_view resource) const;
    [[nodiscard]] std::vector<std::string> upstream_resources(std::string_view resource) const;
    // Resources that can never be produced: every recipe making them
    // transitively requires an unproducible input (cycle or dangling).
    // Sorted for determinism.
    [[nodiscard]] const std::vector<std::string>& unproducible_resources() const {
        return unproducible_;
    }
    [[nodiscard]] const std::vector<Edge>& edges() const { return edges_; }

private:
    friend class EconomyCatalog;
    // BFS over resource -> recipe -> resources. downstream=true follows
    // consumers_of + recipe_outputs_; false follows producers_of +
    // recipe_inputs_. The start node is not protected from re-entry:
    // reaching it again means a cycle, surfaced in the result.
    [[nodiscard]] std::vector<std::string> closure(std::string_view start,
                                                   bool downstream) const;
    std::vector<Edge> edges_; // sorted by (recipe, from, to)
    std::unordered_map<std::string, std::vector<std::string>> producers_;
    std::unordered_map<std::string, std::vector<std::string>> consumers_;
    // recipe -> sorted resource lists, for closure queries.
    std::unordered_map<std::string, std::vector<std::string>> recipe_outputs_;
    std::unordered_map<std::string, std::vector<std::string>> recipe_inputs_;
    std::vector<std::string> unproducible_; // fixpoint result, sorted
};

// Validated definition registry. define()/add_recipe() never throw on
// bad content — malformed or duplicate ids are recorded as issues and
// surfaced by validate() together with the structural checks. Callers
// decide whether warnings are acceptable (editor preview) or fatal
// (package load).
class EconomyCatalog {
public:
    // Records an issue and skips storing when the id is malformed; on a
    // duplicate id records an issue and keeps the first definition.
    void define(ResourceSpec resource);
    void add_recipe(RecipeSpec recipe);
    void clear();

    [[nodiscard]] const ResourceSpec* resource(std::string_view id) const;
    [[nodiscard]] const RecipeSpec* recipe(std::string_view id) const;
    [[nodiscard]] std::vector<const ResourceSpec*> resources() const; // sorted by id
    [[nodiscard]] std::vector<const RecipeSpec*> recipes() const;     // sorted by id
    [[nodiscard]] std::size_t resource_count() const { return resources_.size(); }
    [[nodiscard]] std::size_t recipe_count() const { return recipes_.size(); }

    // Full validation pass: malformed/duplicate ids, unknown references,
    // impossible/zero-output recipes, dependency cycles, unreachable
    // chains, invalid units. Deterministic issue order (sorted records).
    [[nodiscard]] std::vector<ValidationIssue> validate() const;

    // Lazily built; invalidated by define/add_recipe/clear.
    [[nodiscard]] const EconomyGraph& graph() const;

private:
    void rebuild_graph() const;

    std::unordered_map<std::string, ResourceSpec> resources_;
    std::unordered_map<std::string, RecipeSpec> recipes_;
    std::vector<ValidationIssue> recorded_issues_; // load-time rejects
    mutable std::optional<EconomyGraph> graph_;
};

// Observed runtime quantities for one resource over a recent window —
// supplied by the caller (colony/galaxy rollups), never measured here.
struct ResourceObservation {
    double produced_per_day{0.0};
    double consumed_per_day{0.0};
    double stock{0.0};
    double capacity_per_day{0.0}; // installed production capability, 0 = unknown
};

struct EconomyDiagnostic {
    std::string resource;
    double demand_per_day{0.0};
    double supply_per_day{0.0};
    double unmet_per_day{0.0};
    double reserve_days{0.0};     // stock / consumed_per_day
    double utilization{0.0};      // produced / capacity_per_day (NaN-safe: 0)
    bool import_dependent{false}; // demanded, but no recipe produces it
    bool bottleneck{false};       // unmet demand and/or at-capacity shortage
};

// Demand-driven analysis: which resources bottleneck, which depend on
// imports, what reserves remain. Deterministic — output rows sorted by
// resource id. demand pairs list required_per_day per resource.
[[nodiscard]] std::vector<EconomyDiagnostic>
analyze_economy(const EconomyCatalog& catalog,
                const std::vector<ResourceAmount>& demand,
                const std::unordered_map<std::string, ResourceObservation>& observed);

// Bridge to the runtime executor: converts a validated RecipeSpec into
// the ResourceNetwork Recipe shape (inputs + outputs + byproducts,
// duration scaled by 1/efficiency is NOT applied — efficiency multiplies
// output amounts instead, matching Recipe's fixed-quantity contract).
[[nodiscard]] Recipe to_runtime_recipe(const RecipeSpec& spec);

} // namespace stellar::engine
