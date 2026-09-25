#include <stellar/engine/economy_catalog.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

ResourceSpec res(std::string id, ResourceCategory cat, double mass = 1.0) {
    ResourceSpec r;
    r.id = std::move(id);
    r.name_key = "RES";
    r.category = cat;
    r.physical = cat != ResourceCategory::Energy &&
                 cat != ResourceCategory::Information &&
                 cat != ResourceCategory::Abstract &&
                 cat != ResourceCategory::Labor;
    r.mass_per_unit = r.physical ? mass : 0.0;
    r.transportable = r.physical;
    r.storage = r.physical ? StorageClass::Bulk : StorageClass::None;
    return r;
}

RecipeSpec recipe(std::string id, std::vector<ResourceAmount> in,
                  std::vector<ResourceAmount> out, double days = 1.0) {
    RecipeSpec r;
    r.id = std::move(id);
    r.inputs = std::move(in);
    r.outputs = std::move(out);
    r.duration_days = days;
    return r;
}

// ore -> steel -> hull_plate -> ship_hull; hydrogen+energy -> propellant
EconomyCatalog valid_catalog() {
    EconomyCatalog c;
    c.define(res("res.ore", ResourceCategory::Raw));
    c.define(res("res.steel", ResourceCategory::Refined));
    c.define(res("res.hull_plate", ResourceCategory::Component));
    c.define(res("res.ship_hull", ResourceCategory::Component));
    c.define(res("res.hydrogen", ResourceCategory::Raw));
    c.define(res("res.propellant", ResourceCategory::Refined));
    c.define(res("res.energy", ResourceCategory::Energy));
    c.add_recipe(recipe("smelt", {{"res.ore", 2.0}}, {{"res.steel", 1.0}}, 2.0));
    c.add_recipe(recipe("roll_plate", {{"res.steel", 2.0}},
                        {{"res.hull_plate", 1.0}}));
    c.add_recipe(recipe("assemble_hull", {{"res.hull_plate", 4.0}},
                        {{"res.ship_hull", 1.0}}, 4.0));
    auto refine = recipe("crack_hydrogen", {{"res.hydrogen", 3.0}},
                         {{"res.propellant", 2.0}});
    refine.energy_units = 5.0;
    refine.energy_resource = "res.energy";
    c.add_recipe(std::move(refine));
    return c;
}

bool has_issue(const std::vector<ValidationIssue>& issues,
               ValidationSeverity sev, std::string_view record,
               std::string_view field) {
    for (const auto& i : issues)
        if (i.severity == sev && i.record == record && i.field == field)
            return true;
    return false;
}

} // namespace

int main() {
    // Valid catalog passes clean.
    {
        const auto c = valid_catalog();
        const auto issues = c.validate();
        for (const auto& i : issues)
            std::cerr << "unexpected issue: " << i.record << "/" << i.field
                      << ": " << i.reason << '\n';
        check(issues.empty(), "valid catalog has no issues");
    }

    // Graph queries.
    {
        const auto c = valid_catalog();
        const auto& g = c.graph();
        check(g.producers_of("res.steel") == std::vector<std::string>{"smelt"},
              "producer lookup");
        check(g.consumers_of("res.steel").size() == 1, "consumer lookup");
        const auto down = g.downstream_resources("res.ore");
        check(down.size() == 3, "ore reaches steel/plate/hull downstream");
        const auto up = g.upstream_resources("res.ship_hull");
        check(up.size() == 3, "hull traces back to plate/steel/ore");
        check(g.unproducible_resources().empty(),
              "valid catalog has no unproducible resources");
    }

    // Validation: duplicate and malformed ids, unknown refs, zero-output.
    {
        EconomyCatalog c;
        c.define(res("res.a", ResourceCategory::Raw));
        c.define(res("res.a", ResourceCategory::Raw));           // duplicate
        c.define(res("bad id!", ResourceCategory::Raw));         // malformed
        c.add_recipe(recipe("r1", {{"res.a", 1.0}}, {}));  // zero-output
        c.add_recipe(recipe("r2", {{"res.missing", 1.0}},
                            {{"res.a", 1.0}}));            // unknown input
        auto bad = recipe("r3", {{"res.a", -2.0}}, {{"res.a", 1.0}});
        bad.duration_days = 0.0;                            // bad duration
        c.add_recipe(std::move(bad));
        const auto issues = c.validate();
        check(has_issue(issues, ValidationSeverity::Error, "res.a", "id"),
              "duplicate id reported");
        check(has_issue(issues, ValidationSeverity::Error, "bad id!", "id"),
              "malformed id reported");
        check(has_issue(issues, ValidationSeverity::Error, "r1", "outputs"),
              "zero-output recipe reported");
        check(has_issue(issues, ValidationSeverity::Error, "r2", "inputs"),
              "unknown resource ref reported");
        check(has_issue(issues, ValidationSeverity::Error, "r3", "inputs"),
              "nonpositive amount reported");
        check(has_issue(issues, ValidationSeverity::Error, "r3", "duration_days"),
              "nonpositive duration reported");
    }

    // Dependency cycle + unreachable chain.
    {
        EconomyCatalog c;
        c.define(res("res.a", ResourceCategory::Refined));
        c.define(res("res.b", ResourceCategory::Refined));
        c.define(res("res.ore", ResourceCategory::Raw));
        c.define(res("res.exotic", ResourceCategory::Component)); // no recipe at all
        c.add_recipe(recipe("a_to_b", {{"res.a", 1.0}}, {{"res.b", 1.0}}));
        c.add_recipe(recipe("b_to_a", {{"res.b", 1.0}}, {{"res.a", 1.0}}));
        const auto issues = c.validate();
        check(has_issue(issues, ValidationSeverity::Error, "res.a", "graph"),
              "cycle member a reported");
        check(has_issue(issues, ValidationSeverity::Error, "res.b", "graph"),
              "cycle member b reported");
        check(has_issue(issues, ValidationSeverity::Warning, "res.exotic",
                        "outputs"),
              "unproducible resource reported");
        check(has_issue(issues, ValidationSeverity::Warning, "res.a", "outputs") &&
                  has_issue(issues, ValidationSeverity::Warning, "res.b", "outputs"),
              "cycle members are also unreachable");
    }

    // Substitution and energy warnings.
    {
        EconomyCatalog c;
        c.define(res("res.ore", ResourceCategory::Raw));
        c.define(res("res.steel", ResourceCategory::Refined));
        auto r = recipe("smelt", {{"res.ore", 2.0}}, {{"res.steel", 1.0}});
        r.allow_substitution = true; // but res.ore has no substitution_group
        r.energy_units = 3.0;        // no energy resource in catalog
        c.add_recipe(std::move(r));
        const auto issues = c.validate();
        check(has_issue(issues, ValidationSeverity::Warning, "smelt", "inputs"),
              "substitution without group warned");
        check(has_issue(issues, ValidationSeverity::Warning, "smelt",
                        "energy_units"),
              "energy requirement without energy resource warned");
    }

    // Diagnostics: bottleneck, import dependence, reserves, utilization.
    {
        const auto c = valid_catalog();
        std::unordered_map<std::string, ResourceObservation> observed;
        observed["res.steel"] = {4.0, 10.0, 30.0, 4.0};   // short, maxed
        observed["res.ore"] = {12.0, 8.0, 100.0, 20.0};   // surplus
        const auto diag = analyze_economy(
            c, {{"res.steel", 10.0}, {"res.ore", 8.0}, {"res.food", 5.0}},
            observed);
        check(diag.size() == 3, "diagnostic row per demanded/observed resource");
        const auto& steel = diag[2]; // sorted: food, ore, steel
        check(steel.resource == "res.steel" && steel.bottleneck &&
                  std::abs(steel.unmet_per_day - 6.0) < 1e-9,
              "steel bottleneck with 6/day unmet");
        check(std::abs(steel.reserve_days - 3.0) < 1e-9, "steel 3 days reserve");
        check(std::abs(steel.utilization - 1.0) < 1e-9, "steel at capacity");
        check(diag[0].resource == "res.food" && diag[0].import_dependent,
              "food import-dependent (no recipe)");
        check(!diag[1].bottleneck, "ore supply meets demand");
    }

    // Live consumption: catalog recipes drive the runtime ResourceNetwork.
    {
        const auto c = valid_catalog();
        ResourceNetwork net;
        for (const ResourceSpec* r : c.resources())
            net.define({r->id, r->name_key, r->physical});
        for (const RecipeSpec* r : c.recipes())
            net.add_recipe(to_runtime_recipe(*r));
        auto& node = net.add_node(1);
        node.inventory.add("res.ore", 8.0);
        net.add_producer(1, "smelt");
        net.advance(2.0); // one smelt run: -2 ore, +1 steel
        check(std::abs(node.inventory.quantity("res.steel") - 1.0) < 1e-9,
              "catalog recipe produced steel through runtime");
        check(std::abs(node.inventory.quantity("res.ore") - 6.0) < 1e-9,
              "inputs consumed");
    }

    // Deterministic issue ordering: identical catalogs, identical output.
    {
        EconomyCatalog a;
        a.define(res("res.z", ResourceCategory::Refined));
        a.define(res("res.a", ResourceCategory::Refined));
        a.add_recipe(recipe("r1", {{"res.z", 1.0}}, {{"res.a", 1.0}}));
        a.add_recipe(recipe("r2", {{"res.a", 1.0}}, {{"res.z", 1.0}}));
        EconomyCatalog b;
        b.define(res("res.a", ResourceCategory::Refined));
        b.define(res("res.z", ResourceCategory::Refined));
        b.add_recipe(recipe("r2", {{"res.a", 1.0}}, {{"res.z", 1.0}}));
        b.add_recipe(recipe("r1", {{"res.z", 1.0}}, {{"res.a", 1.0}}));
        const auto ia = a.validate();
        const auto ib = b.validate();
        bool same = ia.size() == ib.size();
        for (std::size_t i = 0; same && i < ia.size(); ++i)
            same = ia[i].record == ib[i].record && ia[i].field == ib[i].field &&
                   ia[i].reason == ib[i].reason;
        check(same, "validation order independent of insertion order");
    }

    if (failures != 0) {
        std::cerr << failures << " economy catalog checks failed\n";
        return 1;
    }
    std::cout << "EconomyCatalog validation/graph/diagnostics tests passed\n";
    return 0;
}
