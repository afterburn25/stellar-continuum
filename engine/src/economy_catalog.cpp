#include <stellar/engine/economy_catalog.hpp>

#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <unordered_set>

namespace stellar::engine {

namespace {

bool valid_id(std::string_view id) {
    if (id.empty()) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

void collect_refs(const RecipeSpec& recipe,
                  std::vector<std::pair<std::string_view, const char*>>& out) {
    for (const auto& a : recipe.inputs) out.emplace_back(a.resource, "inputs");
    for (const auto& a : recipe.outputs) out.emplace_back(a.resource, "outputs");
    for (const auto& a : recipe.catalysts) out.emplace_back(a.resource, "catalysts");
    for (const auto& a : recipe.byproducts) out.emplace_back(a.resource, "byproducts");
}

} // namespace

// ---------------------------------------------------------------------
// EconomyGraph

std::vector<std::string> EconomyGraph::producers_of(std::string_view resource) const {
    const auto it = producers_.find(std::string(resource));
    return it == producers_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> EconomyGraph::consumers_of(std::string_view resource) const {
    const auto it = consumers_.find(std::string(resource));
    return it == consumers_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> EconomyGraph::closure(std::string_view start,
                                               bool downstream) const {
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> visited_recipes;
    std::vector<std::string> result;
    std::queue<std::string> frontier;
    frontier.push(std::string(start));
    const auto& recipe_map = downstream ? recipe_outputs_ : recipe_inputs_;
    while (!frontier.empty()) {
        const std::string res = frontier.front();
        frontier.pop();
        const auto recipes = downstream ? consumers_of(res) : producers_of(res);
        for (const auto& recipe_id : recipes) {
            if (!visited_recipes.insert(recipe_id).second) continue;
            const auto it = recipe_map.find(recipe_id);
            if (it == recipe_map.end()) continue;
            for (const auto& next : it->second)
                if (seen.insert(next).second) {
                    result.push_back(next);
                    frontier.push(next);
                }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string>
EconomyGraph::downstream_resources(std::string_view resource) const {
    return closure(resource, true);
}

std::vector<std::string>
EconomyGraph::upstream_resources(std::string_view resource) const {
    return closure(resource, false);
}

// ---------------------------------------------------------------------
// EconomyCatalog

void EconomyCatalog::define(ResourceSpec resource) {
    if (!valid_id(resource.id)) {
        recorded_issues_.push_back({ValidationSeverity::Error, resource.id, "id",
                                    "malformed resource id"});
        return;
    }
    const std::string id = resource.id;
    if (!resources_.emplace(id, std::move(resource)).second)
        recorded_issues_.push_back({ValidationSeverity::Error, id, "id",
                                    "duplicate resource id (kept first)"});
    graph_.reset();
}

void EconomyCatalog::add_recipe(RecipeSpec recipe) {
    if (!valid_id(recipe.id)) {
        recorded_issues_.push_back({ValidationSeverity::Error, recipe.id, "id",
                                    "malformed recipe id"});
        return;
    }
    const std::string id = recipe.id;
    if (!recipes_.emplace(id, std::move(recipe)).second)
        recorded_issues_.push_back({ValidationSeverity::Error, id, "id",
                                    "duplicate recipe id (kept first)"});
    graph_.reset();
}

void EconomyCatalog::clear() {
    resources_.clear();
    recipes_.clear();
    recorded_issues_.clear();
    graph_.reset();
}

const ResourceSpec* EconomyCatalog::resource(std::string_view id) const {
    const auto it = resources_.find(std::string(id));
    return it == resources_.end() ? nullptr : &it->second;
}

const RecipeSpec* EconomyCatalog::recipe(std::string_view id) const {
    const auto it = recipes_.find(std::string(id));
    return it == recipes_.end() ? nullptr : &it->second;
}

std::vector<const ResourceSpec*> EconomyCatalog::resources() const {
    std::vector<const ResourceSpec*> out;
    out.reserve(resources_.size());
    for (const auto& [_, r] : resources_) out.push_back(&r);
    std::sort(out.begin(), out.end(),
              [](const ResourceSpec* a, const ResourceSpec* b) { return a->id < b->id; });
    return out;
}

std::vector<const RecipeSpec*> EconomyCatalog::recipes() const {
    std::vector<const RecipeSpec*> out;
    out.reserve(recipes_.size());
    for (const auto& [_, r] : recipes_) out.push_back(&r);
    std::sort(out.begin(), out.end(),
              [](const RecipeSpec* a, const RecipeSpec* b) { return a->id < b->id; });
    return out;
}

std::vector<ValidationIssue> EconomyCatalog::validate() const {
    std::vector<ValidationIssue> issues = recorded_issues_;
    const auto error = [&](std::string record, std::string field, std::string reason) {
        issues.push_back({ValidationSeverity::Error, std::move(record),
                          std::move(field), std::move(reason)});
    };
    const auto warn = [&](std::string record, std::string field, std::string reason) {
        issues.push_back({ValidationSeverity::Warning, std::move(record),
                          std::move(field), std::move(reason)});
    };

    for (const ResourceSpec* r : resources()) {
        if (r->physical && r->mass_per_unit <= 0.0)
            error(r->id, "mass_per_unit", "physical resource needs positive mass");
        if (!r->physical && (r->mass_per_unit > 0.0 || r->volume_per_unit > 0.0))
            warn(r->id, "mass_per_unit", "nonphysical resource carries mass/volume");
        if (r->decay_per_day < 0.0)
            error(r->id, "decay_per_day", "negative decay");
        if (r->base_value < 0.0)
            error(r->id, "base_value", "negative base value");
        if (!r->physical && r->transportable)
            warn(r->id, "transportable", "nonphysical resource flagged transportable");
        if (!r->substitution_group.empty() && !valid_id(r->substitution_group))
            error(r->id, "substitution_group", "malformed substitution group");
    }

    bool has_energy_resource = false;
    for (const ResourceSpec* r : resources())
        if (r->category == ResourceCategory::Energy) has_energy_resource = true;

    for (const RecipeSpec* recipe : recipes()) {
        std::vector<std::pair<std::string_view, const char*>> refs;
        collect_refs(*recipe, refs);
        for (const auto& [ref, field] : refs)
            if (!resources_.count(std::string(ref)))
                error(recipe->id, field,
                      "unknown resource '" + std::string(ref) + "'");
        const auto check_amounts = [&](const std::vector<ResourceAmount>& amounts,
                                       const char* field) {
            for (const auto& a : amounts)
                if (a.amount <= 0.0)
                    error(recipe->id, field,
                          "nonpositive amount for '" + a.resource + "'");
        };
        check_amounts(recipe->inputs, "inputs");
        check_amounts(recipe->outputs, "outputs");
        check_amounts(recipe->catalysts, "catalysts");
        check_amounts(recipe->byproducts, "byproducts");
        if (recipe->outputs.empty() && recipe->byproducts.empty())
            error(recipe->id, "outputs", "zero-output recipe");
        if (recipe->duration_days <= 0.0)
            error(recipe->id, "duration_days", "nonpositive duration");
        if (recipe->efficiency <= 0.0)
            error(recipe->id, "efficiency", "nonpositive efficiency");
        if (recipe->labor_persons < 0.0)
            error(recipe->id, "labor_persons", "negative labor");
        if (recipe->energy_units > 0.0) {
            if (!recipe->energy_resource.empty() &&
                !resources_.count(recipe->energy_resource))
                error(recipe->id, "energy_resource", "unknown energy resource");
            else if (recipe->energy_resource.empty() && !has_energy_resource)
                warn(recipe->id, "energy_units",
                     "energy required but catalog defines no energy resource");
        }
        if (recipe->allow_substitution)
            for (const auto& a : recipe->inputs)
                if (const auto* r = resource(a.resource);
                    r && r->substitution_group.empty())
                    warn(recipe->id, "inputs",
                         "substitution enabled but '" + a.resource +
                             "' has no substitution_group");
    }

    // Dependency cycles: resources reachable from themselves.
    const auto& g = graph();
    std::set<std::string> cycled;
    for (const auto& edge : g.edges()) {
        if (cycled.count(edge.from)) continue;
        const auto downstream = g.downstream_resources(edge.from);
        if (std::find(downstream.begin(), downstream.end(), edge.from) !=
            downstream.end())
            cycled.insert(edge.from);
    }
    for (const auto& id : cycled)
        error(id, "graph", "dependency cycle through production chain");

    for (const auto& id : g.unproducible_resources())
        warn(id, "outputs", "unreachable production chain — no viable recipe path");

    std::sort(issues.begin(), issues.end(), [](const ValidationIssue& a,
                                               const ValidationIssue& b) {
        if (a.severity != b.severity) return a.severity < b.severity;
        if (a.record != b.record) return a.record < b.record;
        if (a.field != b.field) return a.field < b.field;
        return a.reason < b.reason;
    });
    return issues;
}

const EconomyGraph& EconomyCatalog::graph() const {
    if (!graph_) rebuild_graph();
    return *graph_;
}

void EconomyCatalog::rebuild_graph() const {
    EconomyGraph g;
    for (const RecipeSpec* recipe : recipes()) {
        std::set<std::string> outputs;
        for (const auto& a : recipe->outputs) outputs.insert(a.resource);
        for (const auto& a : recipe->byproducts) outputs.insert(a.resource);
        std::set<std::string> consumed;
        for (const auto& a : recipe->inputs) consumed.insert(a.resource);
        for (const auto& a : recipe->catalysts) consumed.insert(a.resource);
        g.recipe_outputs_[recipe->id] = {outputs.begin(), outputs.end()};
        g.recipe_inputs_[recipe->id] = {consumed.begin(), consumed.end()};
        for (const auto& out : outputs) g.producers_[out].push_back(recipe->id);
        for (const auto& in : consumed) {
            g.consumers_[in].push_back(recipe->id);
            for (const auto& out : outputs)
                g.edges_.push_back({recipe->id, in, out});
        }
    }
    for (auto& [_, list] : g.producers_) std::sort(list.begin(), list.end());
    for (auto& [_, list] : g.consumers_) std::sort(list.begin(), list.end());
    std::sort(g.edges_.begin(), g.edges_.end(), [](const auto& a, const auto& b) {
        if (a.recipe != b.recipe) return a.recipe < b.recipe;
        if (a.from != b.from) return a.from < b.from;
        return a.to < b.to;
    });

    // Unproducible fixpoint: chain categories (Refined/Component/
    // Consumable) must be reachable through recipes; Raw is extracted
    // and Energy/Labor/Information/Abstract come from facilities and
    // population — those are sources, not recipe products. A recipe
    // becomes viable once every input and catalyst is available.
    const auto chain_produced = [](ResourceCategory c) {
        return c == ResourceCategory::Refined ||
               c == ResourceCategory::Component ||
               c == ResourceCategory::Consumable;
    };
    std::unordered_set<std::string> available;
    for (const ResourceSpec* r : resources())
        if (!chain_produced(r->category)) available.insert(r->id);
    bool progress = true;
    while (progress) {
        progress = false;
        for (const RecipeSpec* recipe : recipes()) {
            const auto& needed = g.recipe_inputs_[recipe->id];
            if (!std::all_of(needed.begin(), needed.end(),
                             [&](const std::string& id) {
                                 return available.count(id) != 0;
                             }))
                continue;
            const auto& made = g.recipe_outputs_[recipe->id];
            for (const auto& id : made)
                progress |= available.insert(id).second;
        }
    }
    for (const ResourceSpec* r : resources())
        if (chain_produced(r->category) && !available.count(r->id))
            g.unproducible_.push_back(r->id);
    std::sort(g.unproducible_.begin(), g.unproducible_.end());
    graph_ = std::move(g);
}

// ---------------------------------------------------------------------
// Diagnostics

std::vector<EconomyDiagnostic>
analyze_economy(const EconomyCatalog& catalog,
                const std::vector<ResourceAmount>& demand,
                const std::unordered_map<std::string, ResourceObservation>& observed) {
    std::unordered_map<std::string, double> required;
    for (const auto& d : demand) required[d.resource] += d.amount;

    std::set<std::string> ids;
    for (const auto& [id, _] : required) ids.insert(id);
    for (const auto& [id, _] : observed) ids.insert(id);

    std::vector<EconomyDiagnostic> out;
    for (const auto& id : ids) {
        EconomyDiagnostic row;
        row.resource = id;
        row.demand_per_day = required.count(id) ? required.at(id) : 0.0;
        const auto it = observed.find(id);
        const ResourceObservation obs =
            it == observed.end() ? ResourceObservation{} : it->second;
        row.supply_per_day = obs.produced_per_day;
        row.unmet_per_day = std::max(0.0, row.demand_per_day - row.supply_per_day);
        row.reserve_days = obs.consumed_per_day > 0.0
                               ? obs.stock / obs.consumed_per_day
                               : (obs.stock > 0.0
                                      ? std::numeric_limits<double>::infinity()
                                      : 0.0);
        row.utilization = obs.capacity_per_day > 0.0
                              ? obs.produced_per_day / obs.capacity_per_day
                              : 0.0;
        row.import_dependent =
            row.demand_per_day > 0.0 && catalog.graph().producers_of(id).empty();
        row.bottleneck = row.unmet_per_day > 0.0;
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(),
              [](const EconomyDiagnostic& a, const EconomyDiagnostic& b) {
                  return a.resource < b.resource;
              });
    return out;
}

Recipe to_runtime_recipe(const RecipeSpec& spec) {
    Recipe recipe;
    recipe.id = spec.id;
    recipe.duration_days = spec.duration_days;
    for (const auto& a : spec.inputs) recipe.inputs.emplace_back(a.resource, a.amount);
    const auto scaled = [&spec](const ResourceAmount& a) {
        return std::pair{a.resource, a.amount * spec.efficiency};
    };
    for (const auto& a : spec.outputs) recipe.outputs.push_back(scaled(a));
    for (const auto& a : spec.byproducts) recipe.outputs.push_back(scaled(a));
    return recipe;
}

} // namespace stellar::engine
