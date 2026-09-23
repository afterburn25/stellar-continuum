#include <stellar/engine/colony.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_set>

#include <stellar/engine/resource_economy.hpp>

namespace stellar::engine {

namespace {

bool tags_satisfied(const std::vector<std::string>& required_tags,
                    const std::vector<std::string>& available) {
    for (const auto& req : required_tags) {
        if (std::find(available.begin(), available.end(), req) == available.end()) {
            return false;
        }
    }
    return true;
}

// Ascending-id ordering for deterministic iteration.
template <typename T>
std::vector<const T*> sorted_values(
    const std::unordered_map<std::uint64_t, T>& items) {
    std::vector<const T*> out;
    out.reserve(items.size());
    for (const auto& [id, item] : items) out.push_back(&item);
    std::sort(out.begin(), out.end(),
              [](const T* a, const T* b) { return a->id < b->id; });
    return out;
}

void add_amount(std::map<std::string, double>& into, std::string_view resource,
                double qty) {
    if (qty == 0.0) return;
    into[std::string(resource)] += qty;
}

// utility -> {supply, demand}
using UtilityMap = std::map<std::string, std::pair<double, double>>;

void accumulate_utility(UtilityMap& into,
                        const std::vector<ResourceAmount>& supply,
                        const std::vector<ResourceAmount>& demand) {
    for (const auto& [id, q] : supply) into[id].first += q;
    for (const auto& [id, q] : demand) into[id].second += q;
}

} // namespace

void Colony::define_district(DistrictSpec spec) {
    if (spec.id.empty()) throw std::invalid_argument("district spec id empty");
    if (!district_specs_.emplace(spec.id, std::move(spec)).second) {
        throw std::invalid_argument("duplicate district spec");
    }
}

void Colony::define_structure(StructureSpec spec) {
    if (spec.id.empty()) throw std::invalid_argument("structure spec id empty");
    if (!spec.district.empty() &&
        district_specs_.find(spec.district) == district_specs_.end()) {
        throw std::invalid_argument("structure spec references unknown district");
    }
    if (!structure_specs_.emplace(spec.id, std::move(spec)).second) {
        throw std::invalid_argument("duplicate structure spec");
    }
}

const DistrictSpec* Colony::district_spec(std::string_view id) const {
    auto it = district_specs_.find(std::string(id));
    return it == district_specs_.end() ? nullptr : &it->second;
}

const StructureSpec* Colony::structure_spec(std::string_view id) const {
    auto it = structure_specs_.find(std::string(id));
    return it == structure_specs_.end() ? nullptr : &it->second;
}

bool Colony::build_district(std::uint64_t id, std::string_view spec_id,
                            const std::vector<std::string>& available_tags) {
    const auto* spec = district_spec(spec_id);
    if (!spec || districts_.count(id)) return false;
    if (!tags_satisfied(spec->required_tags, available_tags)) return false;
    District d;
    d.id = id;
    d.spec_id = spec->id;
    d.construction_remaining = spec->build_days;
    d.complete = spec->build_days <= 0.0;
    districts_.emplace(id, std::move(d));
    return true;
}

bool Colony::build_structure(std::uint64_t id, std::string_view spec_id,
                             std::uint64_t district_id,
                             const std::vector<std::string>& available_tags) {
    const auto* spec = structure_spec(spec_id);
    if (!spec || structures_.count(id)) return false;
    if (!tags_satisfied(spec->required_tags, available_tags)) return false;

    if (!spec->district.empty()) {
        const auto* district = this->district(district_id);
        if (!district || district->spec_id != spec->district) return false;
        const auto* dspec = district_spec(district->spec_id);
        if (structures_in(district_id).size() >= dspec->structure_slots) {
            return false;
        }
    } else {
        if (district_id != 0) return false; // standalone spec needs no district
        if (standalone_slots_ != 0) {
            std::size_t standalone = 0;
            for (const auto& [sid, s] : structures_) {
                if (s.district_id == 0) ++standalone;
            }
            if (standalone >= standalone_slots_) return false;
        }
    }

    Structure s;
    s.id = id;
    s.spec_id = spec->id;
    s.district_id = district_id;
    s.construction_remaining = spec->build_days;
    s.complete = spec->build_days <= 0.0;
    structures_.emplace(id, std::move(s));
    return true;
}

bool Colony::demolish_district(std::uint64_t id) {
    if (!structures_in(id).empty()) return false;
    return districts_.erase(id) != 0;
}

bool Colony::demolish_structure(std::uint64_t id) {
    return structures_.erase(id) != 0;
}

bool Colony::set_enabled(std::uint64_t structure_id, bool enabled) {
    auto it = structures_.find(structure_id);
    if (it == structures_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

bool Colony::set_district_enabled(std::uint64_t district_id, bool enabled) {
    auto it = districts_.find(district_id);
    if (it == districts_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

const District* Colony::district(std::uint64_t id) const {
    auto it = districts_.find(id);
    return it == districts_.end() ? nullptr : &it->second;
}

const Structure* Colony::structure(std::uint64_t id) const {
    auto it = structures_.find(id);
    return it == structures_.end() ? nullptr : &it->second;
}

std::vector<const District*> Colony::districts() const {
    return sorted_values(districts_);
}

std::vector<const Structure*> Colony::structures() const {
    return sorted_values(structures_);
}

std::vector<const Structure*> Colony::structures_in(std::uint64_t district_id) const {
    std::vector<const Structure*> out;
    for (const auto* s : structures()) {
        if (s->district_id == district_id) out.push_back(s);
    }
    return out;
}

double Colony::jobs_total() const {
    double total = 0.0;
    for (const auto& [id, s] : structures_) {
        if (!s.complete || !s.enabled) continue;
        if (const auto* spec = structure_spec(s.spec_id)) total += spec->jobs;
    }
    return total;
}

double Colony::housing_capacity() const {
    double total = 0.0;
    for (const auto& [id, s] : structures_) {
        if (!s.complete || !s.enabled) continue;
        if (const auto* spec = structure_spec(s.spec_id)) total += spec->housing;
    }
    return total;
}

std::vector<std::pair<std::string, std::pair<double, double>>>
Colony::utility_balance() const {
    UtilityMap map;
    for (const auto& [id, d] : districts_) {
        if (!d.complete || !d.enabled) continue;
        if (const auto* spec = district_spec(d.spec_id)) {
            accumulate_utility(map, {}, spec->utility_demand_per_day);
        }
    }
    for (const auto& [id, s] : structures_) {
        if (!s.complete || !s.enabled) continue;
        if (const auto* spec = structure_spec(s.spec_id)) {
            accumulate_utility(map, spec->utility_supply_per_day,
                               spec->utility_demand_per_day);
        }
    }
    return {map.begin(), map.end()};
}

ColonyDelta Colony::advance(double elapsed_days, const ColonyInputs& inputs) {
    ColonyDelta delta;
    delta.elapsed_days = elapsed_days;
    if (elapsed_days <= 0.0) return delta;

    // 1. Construction progress (ascending id, deterministic).
    for (const auto* d : districts()) {
        District& mut = districts_[d->id];
        if (!mut.complete) {
            mut.construction_remaining -= elapsed_days;
            if (mut.construction_remaining <= 0.0) {
                mut.construction_remaining = 0.0;
                mut.complete = true;
                ++delta.districts_completed;
            }
        }
    }
    for (const auto* s : structures()) {
        Structure& mut = structures_[s->id];
        if (!mut.complete) {
            mut.construction_remaining -= elapsed_days;
            if (mut.construction_remaining <= 0.0) {
                mut.construction_remaining = 0.0;
                mut.complete = true;
                ++delta.structures_completed;
            }
        }
    }

    // 2. Utility pools: satisfaction fraction per utility shared by all
    //    consumers (aggregate pool — no ordering dependence).
    const auto balance = utility_balance();
    std::unordered_map<std::string, double> satisfaction;
    for (const auto& [id, sd] : balance) {
        const double ratio = sd.second <= 0.0 ? 1.0
                             : sd.first <= 0.0 ? 0.0
                                               : std::min(1.0, sd.first / sd.second);
        satisfaction[id] = ratio;
    }
    delta.utilities = balance;

    // 3. Workforce: jobs scale uniformly when workers are short.
    const double jobs_demand = jobs_total();
    const double worker_ratio =
        jobs_demand <= 0.0 ? 1.0
                           : std::min(1.0, inputs.workers_available / jobs_demand);
    delta.jobs_total = jobs_demand;
    delta.jobs_filled = jobs_demand * worker_ratio;
    delta.housing_capacity = housing_capacity();

    // 4. Per-structure pass, ascending id: upkeep, inputs, outputs,
    //    condition, operating ratio.
    std::map<std::string, double> outputs;
    std::map<std::string, double> upkeep_shortfall;
    std::map<std::string, double> input_shortfall;

    for (const auto* s : structures()) {
        Structure& st = structures_[s->id];
        const auto* spec = structure_spec(st.spec_id);
        if (!spec || !st.complete) continue;

        if (!st.enabled) {
            st.operating = 0.0;
            st.condition =
                std::clamp(st.condition - spec->condition_decay_per_day * 0.5 *
                                              elapsed_days,
                           0.0, 1.0);
            continue;
        }

        // District gating: a hosted structure stalls while its district is
        // incomplete or disabled.
        double district_ratio = 1.0;
        if (st.district_id != 0) {
            if (const auto* d = district(st.district_id);
                !d || !d->complete || !d->enabled) {
                district_ratio = 0.0;
            }
        }

        // Utility ratio for this structure's demands.
        double utility_ratio = 1.0;
        for (const auto& [id, q] : spec->utility_demand_per_day) {
            if (q <= 0.0) continue;
            utility_ratio = std::min(utility_ratio, satisfaction[id]);
        }

        double desired = district_ratio * utility_ratio *
                         (spec->jobs > 0.0 ? worker_ratio : 1.0);
        st.operating = desired;
        if (desired <= 0.0) {
            st.condition =
                std::clamp(st.condition - spec->condition_decay_per_day *
                                              elapsed_days,
                           0.0, 1.0);
            continue;
        }

        // Inputs: draw scaled by desired operating; shortage scales it down.
        double input_ratio = 1.0;
        if (inputs.stockpile) {
            for (const auto& [res, per_day] : spec->inputs_per_day) {
                const double need = per_day * elapsed_days * desired;
                if (need <= 0.0) continue;
                const double got = inputs.stockpile->remove(res, need);
                if (got < need) {
                    input_ratio = std::min(input_ratio, need > 0.0 ? got / need : 0.0);
                    add_amount(input_shortfall, res, need - got);
                }
            }
        }
        st.operating = desired * input_ratio;

        // Upkeep: drawn at operating level; shortfall halves effective
        // maintenance (condition suffers) rather than stopping output.
        double upkeep_ratio = 1.0;
        if (inputs.stockpile) {
            for (const auto& [res, per_day] : spec->upkeep_per_day) {
                const double need = per_day * elapsed_days;
                if (need <= 0.0) continue;
                const double got = inputs.stockpile->remove(res, need);
                if (got < need) {
                    upkeep_ratio = std::min(upkeep_ratio, got / need);
                    add_amount(upkeep_shortfall, res, need - got);
                }
            }
        }

        for (const auto& [res, per_day] : spec->outputs_per_day) {
            const double qty = per_day * elapsed_days * st.operating;
            if (qty <= 0.0) continue;
            add_amount(outputs, res, qty);
            if (inputs.stockpile) inputs.stockpile->add(res, qty);
        }

        const double eff_maint = inputs.maintenance * upkeep_ratio;
        st.condition = std::clamp(
            st.condition +
                (spec->condition_repair_per_day * eff_maint -
                 spec->condition_decay_per_day * (1.0 - eff_maint)) *
                    elapsed_days,
            0.0, 1.0);
    }

    delta.outputs_produced.assign(outputs.begin(), outputs.end());
    delta.upkeep_shortfall.assign(upkeep_shortfall.begin(), upkeep_shortfall.end());
    delta.input_shortfall.assign(input_shortfall.begin(), input_shortfall.end());
    return delta;
}

Colony::State Colony::capture_state() const {
    State state;
    state.standalone_slots = standalone_slots_;
    for (const District* d : districts())
        state.districts.push_back({d->id, d->spec_id,
                                   d->construction_remaining, d->complete,
                                   d->enabled});
    for (const Structure* s : structures())
        state.structures.push_back({s->id, s->spec_id, s->district_id,
                                    s->construction_remaining, s->complete,
                                    s->enabled, s->condition, s->operating});
    return state;
}

void Colony::restore_state(const State& state) {
    districts_.clear();
    structures_.clear();
    standalone_slots_ = state.standalone_slots;
    for (const DistrictState& d : state.districts) {
        if (!district_specs_.count(d.spec_id))
            throw std::invalid_argument(
                "Colony snapshot references undefined district spec");
        districts_[d.id] = {d.id, d.spec_id, d.construction_remaining,
                            d.complete, d.enabled};
    }
    for (const StructureState& s : state.structures) {
        if (!structure_specs_.count(s.spec_id))
            throw std::invalid_argument(
                "Colony snapshot references undefined structure spec");
        if (s.district_id != 0 && !districts_.count(s.district_id))
            throw std::invalid_argument(
                "Colony snapshot structure references missing district");
        structures_[s.id] = {s.id,
                             s.spec_id,
                             s.district_id,
                             s.construction_remaining,
                             s.complete,
                             s.enabled,
                             s.condition,
                             s.operating};
    }
}

} // namespace stellar::engine
