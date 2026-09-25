#include <stellar/engine/colony.hpp>
#include <stellar/engine/resource_economy.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

using namespace stellar::engine;

DistrictSpec residential() {
    DistrictSpec d;
    d.id = "district.residential";
    d.category = "housing";
    d.structure_slots = 4;
    d.build_days = 10.0;
    d.utility_demand_per_day = {{"power", 2.0}};
    return d;
}

StructureSpec hab_block() {
    StructureSpec s;
    s.id = "structure.hab_block";
    s.district = "district.residential";
    s.build_days = 5.0;
    s.utility_demand_per_day = {{"power", 1.0}};
    s.housing = 1000.0;
    return s;
}

StructureSpec fusion_plant() {
    StructureSpec s;
    s.id = "structure.fusion_plant";
    s.build_days = 20.0;
    s.utility_supply_per_day = {{"power", 10.0}};
    s.jobs = 50.0;
    s.upkeep_per_day = {{"parts", 0.1}};
    s.condition_decay_per_day = 0.001;
    s.condition_repair_per_day = 0.002;
    return s;
}

StructureSpec mine() {
    StructureSpec s;
    s.id = "structure.mine";
    s.build_days = 8.0;
    s.utility_demand_per_day = {{"power", 4.0}};
    s.jobs = 100.0;
    s.inputs_per_day = {{"parts", 1.0}};
    s.outputs_per_day = {{"ore", 5.0}};
    return s;
}

Colony make_colony() {
    Colony c;
    c.define_district(residential());
    c.define_structure(hab_block());
    c.define_structure(fusion_plant());
    c.define_structure(mine());
    return c;
}

} // namespace

int main() {
    // --- Spec registration -------------------------------------------------
    {
        Colony c;
        c.define_district(residential());
        bool threw = false;
        try {
            c.define_district(residential());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "duplicate district spec rejected");

        StructureSpec orphan;
        orphan.id = "structure.orphan";
        orphan.district = "district.nonexistent";
        threw = false;
        try {
            c.define_structure(orphan);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "structure spec with unknown district rejected");
    }

    // --- Construction + district slots -------------------------------------
    {
        Colony c = make_colony();
        check(c.build_district(1, "district.residential"), "build district");
        check(!c.build_district(1, "district.residential"), "duplicate id rejected");
        // Structure requires a district that doesn't exist yet-completed?
        // District exists but incomplete: still valid placement target.
        check(c.build_structure(10, "structure.hab_block", 1),
              "structure placed in district");

        // Fill the district's 4 slots, then a 5th must fail.
        check(c.build_structure(11, "structure.hab_block", 1), "slot 2");
        check(c.build_structure(12, "structure.hab_block", 1), "slot 3");
        check(c.build_structure(13, "structure.hab_block", 1), "slot 4");
        check(!c.build_structure(14, "structure.hab_block", 1),
              "district slots enforced");

        // Standalone spec cannot claim a district.
        check(!c.build_structure(15, "structure.fusion_plant", 1),
              "standalone spec rejects district id");
        check(c.build_structure(15, "structure.fusion_plant", 0),
              "standalone build ok");

        // Requirement tags gate construction.
        StructureSpec gated;
        gated.id = "structure.shield";
        gated.required_tags = {"tech.shields"};
        c.define_structure(gated);
        check(!c.build_structure(16, "structure.shield", 0, {}),
              "missing requirement tag blocks build");
        check(c.build_structure(16, "structure.shield", 0, {"tech.shields"}),
              "requirement tag allows build");
    }

    // --- Construction completion via advance -------------------------------
    {
        Colony c = make_colony();
        c.build_district(1, "district.residential"); // 10 days
        c.build_structure(10, "structure.fusion_plant", 0); // 20 days
        ColonyInputs in;
        ColonyDelta d = c.advance(10.0, in);
        check(d.districts_completed == 1, "district completes at build_days");
        check(!c.structure(10)->complete, "plant still under construction");
        d = c.advance(10.0, in);
        check(d.structures_completed == 1, "plant completes at build_days");
        check(c.structure(10)->complete, "plant complete flag");
    }

    // --- Utilities gate operation -------------------------------------------
    {
        Colony c = make_colony();
        c.build_district(1, "district.residential"); // demands 2 power
        c.build_structure(10, "structure.hab_block", 1); // demands 1
        c.build_structure(20, "structure.mine", 0); // demands 4, jobs 100
        ColonyInputs in;
        in.workers_available = 1000.0; // plant 50 + mine 100 jobs covered
        Inventory stock;
        stock.add("parts", 1e9);
        in.stockpile = &stock;

        c.advance(20.0, in); // finish all construction
        check(c.structure(20)->complete, "mine built");

        // No power plant -> mine operating ratio 0.
        ColonyDelta d = c.advance(1.0, in);
        check(near(c.structure(20)->operating, 0.0), "unpowered mine stalls");

        c.build_structure(30, "structure.fusion_plant", 0); // +10 power
        c.advance(20.0, in);
        d = c.advance(1.0, in);
        // demand 2(district)+1(hab)+4(mine)=7 < supply 10 -> full power.
        check(near(c.structure(20)->operating, 1.0), "powered mine operates");
        // Output: 5 ore/day at full operating for 1 day.
        double ore = 0.0;
        for (const auto& [r, q] : d.outputs_produced)
            if (r == "ore") ore = q;
        check(near(ore, 5.0), "mine produces at capacity");
    }

    // --- Underpowered settlement shares the shortage -------------------------
    {
        Colony c = make_colony();
        // Two mines (4 power each = 8) vs one plant (10) -> after district
        // demand zero, satisfaction = min(1, 10/8) = 1? Build a weak plant.
        StructureSpec weak = fusion_plant();
        weak.id = "structure.weak_plant";
        weak.utility_supply_per_day = {{"power", 4.0}};
        c.define_structure(weak);

        c.build_structure(20, "structure.mine", 0);
        c.build_structure(21, "structure.mine", 0);
        c.build_structure(30, "structure.weak_plant", 0);
        ColonyInputs in;
        in.workers_available = 1000.0;
        Inventory stock;
        stock.add("parts", 1e9);
        in.stockpile = &stock;
        c.advance(20.0, in);
        c.advance(1.0, in);
        // supply 4 / demand 8 -> each mine at 0.5 utility ratio.
        check(near(c.structure(20)->operating, 0.5, 1e-6),
              "shared shortage halves operation");
        check(near(c.structure(21)->operating, 0.5, 1e-6),
              "shortage applied uniformly");
    }

    // --- Workforce shortage scales jobs --------------------------------------
    {
        Colony c = make_colony();
        c.build_structure(30, "structure.fusion_plant", 0); // 50 jobs
        c.build_structure(20, "structure.mine", 0); // 100 jobs
        ColonyInputs in;
        in.workers_available = 75.0; // half of 150
        Inventory stock;
        stock.add("parts", 1e9);
        in.stockpile = &stock;
        c.advance(20.0, in);
        ColonyDelta d = c.advance(1.0, in);
        check(near(d.jobs_total, 150.0), "jobs total reported");
        check(near(d.jobs_filled, 75.0), "half workforce reported");
        check(near(c.structure(20)->operating, 0.5, 1e-6),
              "staffing shortage halves mine");
    }

    // --- Input starvation ----------------------------------------------------
    {
        Colony c = make_colony();
        c.build_structure(30, "structure.fusion_plant", 0);
        c.build_structure(20, "structure.mine", 0);
        ColonyInputs in;
        in.workers_available = 1000.0;
        Inventory stock; // empty: no parts for mine inputs
        in.stockpile = &stock;
        c.advance(20.0, in);
        c.advance(1.0, in);
        check(near(c.structure(20)->operating, 0.0, 1e-6),
              "input-starved mine stalls");
        check(stock.quantity("ore") == 0.0, "no ore produced");
    }

    // --- Condition decay / repair ---------------------------------------------
    {
        Colony c = make_colony();
        c.build_structure(30, "structure.fusion_plant", 0);
        ColonyInputs in;
        in.workers_available = 100.0;
        Inventory stock;
        stock.add("parts", 1e9);
        in.stockpile = &stock;
        in.maintenance = 0.0; // no upkeep funding -> pure decay
        c.advance(20.0, in);
        c.advance(100.0, in);
        const double worn = c.structure(30)->condition;
        check(worn < 1.0, "condition decays without maintenance");
        in.maintenance = 1.0;
        c.advance(50.0, in);
        check(c.structure(30)->condition > worn,
              "maintenance repairs condition");
    }

    // --- Determinism: identical runs produce identical deltas ------------------
    {
        auto run = [] {
            Colony c = make_colony();
            c.build_district(1, "district.residential");
            c.build_structure(10, "structure.hab_block", 1);
            c.build_structure(20, "structure.mine", 0);
            c.build_structure(30, "structure.fusion_plant", 0);
            ColonyInputs in;
            in.workers_available = 120.0;
            Inventory stock;
            stock.add("parts", 500.0);
            in.stockpile = &stock;
            double ore = 0.0;
            for (int t = 0; t < 60; ++t) {
                ColonyDelta d = c.advance(1.0, in);
                for (const auto& [r, q] : d.outputs_produced)
                    if (r == "ore") ore += q;
            }
            return std::pair{ore, stock.quantity("ore")};
        };
        const auto a = run();
        const auto b = run();
        check(a.first == b.first && a.second == b.second,
              "identical runs are bit-equal");
    }

    // --- Demolish / enable -----------------------------------------------------
    {
        Colony c = make_colony();
        c.build_district(1, "district.residential");
        c.build_structure(10, "structure.hab_block", 1);
        check(!c.demolish_district(1), "occupied district cannot be demolished");
        check(c.demolish_structure(10), "structure demolished");
        check(c.demolish_district(1), "empty district demolished");
        c.build_structure(30, "structure.fusion_plant", 0);
        check(c.set_enabled(30, false), "disable structure");
        check(c.jobs_total() == 0.0, "disabled structure offers no jobs");
    }

    // --- Scale: 1000 colonies x ~30 structures ----------------------------------
    {
        const auto t0 = std::chrono::steady_clock::now();
        double checksum = 0.0;
        for (int i = 0; i < 1000; ++i) {
            Colony c = make_colony();
            c.build_district(1, "district.residential");
            for (std::uint64_t s = 10; s < 14; ++s)
                c.build_structure(s, "structure.hab_block", 1);
            for (std::uint64_t s = 20; s < 45; ++s)
                c.build_structure(s, "structure.mine", 0);
            c.build_structure(60, "structure.fusion_plant", 0);
            ColonyInputs in;
            in.workers_available = 2500.0;
            Inventory stock;
            stock.add("parts", 1e6);
            in.stockpile = &stock;
            for (int t = 0; t < 30; ++t) {
                ColonyDelta d = c.advance(1.0, in);
                checksum += d.jobs_filled;
            }
            checksum += stock.quantity("ore");
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        check(checksum > 0.0, "scale run produces output");
        std::cout << "scale: 1000 colonies x 30 ticks in " << ms << " ms\n";
        check(ms < 30000.0, "scale run within budget");
    }

    if (failures == 0) {
        std::cout << "all colony tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
