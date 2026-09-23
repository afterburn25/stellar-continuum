#include <stellar/engine/warfare.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stellar::engine {

namespace {

// Effective attack of a cohort: count × class attack × condition ×
// experience bonus (experience adds up to +50%).
double cohort_attack(const ShipCohort& c, const ShipClass& cls) {
    return c.count * cls.attack * c.condition * (1.0 + 0.5 * c.experience);
}

double cohort_hull(const ShipCohort& c, const ShipClass& cls) {
    return c.count * cls.hull * c.condition;
}

} // namespace

void WarfareModel::define_class(ShipClass cls) {
    if (cls.id.empty()) throw std::invalid_argument("ship class id empty");
    if (cls.hull <= 0.0 || cls.speed <= 0.0) {
        throw std::invalid_argument("ship class needs positive hull+speed");
    }
    if (!classes_.emplace(cls.id, std::move(cls)).second) {
        throw std::invalid_argument("duplicate ship class");
    }
}

const ShipClass* WarfareModel::ship_class(std::string_view id) const {
    auto it = classes_.find(std::string(id));
    return it == classes_.end() ? nullptr : &it->second;
}

bool WarfareModel::add_fleet(std::uint64_t id, std::uint64_t owner,
                             double x, double y) {
    if (fleets_.count(id)) return false;
    FleetState f;
    f.id = id;
    f.owner = owner;
    f.x = x;
    f.y = y;
    fleets_.emplace(id, f);
    return true;
}

bool WarfareModel::remove_fleet(std::uint64_t id) {
    cohorts_.erase(id);
    return fleets_.erase(id) != 0;
}

bool WarfareModel::add_ships(std::uint64_t fleet_id,
                             std::string_view class_id, double count,
                             double condition, double experience) {
    if (!fleets_.count(fleet_id) || !ship_class(class_id) || count <= 0.0 ||
        condition <= 0.0 || condition > 1.0) {
        return false;
    }
    auto& list = cohorts_[fleet_id];
    for (auto& c : list) {
        if (c.ship_class == class_id) {
            // Weighted merge so repeated adds keep representative state.
            const double total = c.count + count;
            c.condition =
                (c.condition * c.count + condition * count) / total;
            c.experience =
                (c.experience * c.count + experience * count) / total;
            c.count = total;
            return true;
        }
    }
    ShipCohort c;
    c.ship_class = std::string(class_id);
    c.count = count;
    c.condition = condition;
    c.experience = experience;
    list.push_back(std::move(c));
    return true;
}

bool WarfareModel::remove_ships(std::uint64_t fleet_id,
                                std::string_view class_id, double count) {
    auto it = cohorts_.find(fleet_id);
    if (it == cohorts_.end()) return false;
    auto& list = it->second;
    for (auto c = list.begin(); c != list.end(); ++c) {
        if (c->ship_class == class_id) {
            if (count <= 0.0 || count > c->count) return false;
            c->count -= count;
            if (c->count <= 0.0) list.erase(c);
            return true;
        }
    }
    return false;
}

bool WarfareModel::set_order(std::uint64_t fleet_id, FleetOrder order) {
    auto it = fleets_.find(fleet_id);
    if (it == fleets_.end()) return false;
    it->second.order = order;
    return true;
}

bool WarfareModel::set_position(std::uint64_t fleet_id, double x, double y) {
    auto it = fleets_.find(fleet_id);
    if (it == fleets_.end()) return false;
    it->second.x = x;
    it->second.y = y;
    return true;
}

const FleetState* WarfareModel::fleet(std::uint64_t id) const {
    auto it = fleets_.find(id);
    return it == fleets_.end() ? nullptr : &it->second;
}

const ShipCohort* WarfareModel::cohort(std::uint64_t fleet_id,
                                       std::string_view class_id) const {
    const auto it = cohorts_.find(fleet_id);
    if (it == cohorts_.end()) return nullptr;
    for (const auto& c : it->second) {
        if (c.ship_class == class_id) return &c;
    }
    return nullptr;
}

std::vector<const FleetState*> WarfareModel::fleets() const {
    std::vector<const FleetState*> out;
    out.reserve(fleets_.size());
    for (const auto& [id, f] : fleets_) out.push_back(&f);
    std::sort(out.begin(), out.end(),
              [](const FleetState* a, const FleetState* b) {
                  return a->id < b->id;
              });
    return out;
}

std::vector<const ShipCohort*>
WarfareModel::cohorts(std::uint64_t fleet_id) const {
    std::vector<const ShipCohort*> out;
    const auto it = cohorts_.find(fleet_id);
    if (it == cohorts_.end()) return out;
    for (const auto& c : it->second) out.push_back(&c);
    std::sort(out.begin(), out.end(), [](const ShipCohort* a,
                                         const ShipCohort* b) {
        return a->ship_class < b->ship_class;
    });
    return out;
}

FleetReport WarfareModel::report(std::uint64_t fleet_id) const {
    FleetReport r;
    const auto it = cohorts_.find(fleet_id);
    if (it == cohorts_.end()) return r;
    r.speed = std::numeric_limits<double>::infinity();
    for (const auto& c : it->second) {
        const auto* cls = ship_class(c.ship_class);
        if (!cls || c.count <= 0.0) continue;
        r.ships += c.count;
        r.attack += cohort_attack(c, *cls);
        r.hull += cohort_hull(c, *cls);
        r.speed = std::min(r.speed, cls->speed * c.condition);
        r.interdiction_radius += c.count * c.condition * cls->interdiction;
        r.supply_per_day += c.count * cls->supply_per_day;
    }
    if (r.ships <= 0.0) r.speed = 0.0;
    return r;
}

double WarfareModel::interdiction_radius(std::uint64_t fleet_id) const {
    return report(fleet_id).interdiction_radius;
}

bool WarfareModel::interdicted(double x, double y, std::uint64_t mover_owner,
                               std::uint64_t except_fleet) const {
    for (const auto& [id, f] : fleets_) {
        if (id == except_fleet || f.owner == mover_owner) continue;
        if (f.order.kind != FleetOrderKind::Interdict) continue;
        const double r = interdiction_radius(id);
        if (r <= 0.0) continue;
        const double dx = f.x - x, dy = f.y - y;
        if (dx * dx + dy * dy <= r * r) return true;
    }
    return false;
}

void WarfareModel::advance(double elapsed_days) {
    if (elapsed_days <= 0.0) return;
    for (const auto* f : fleets()) {
        FleetState& mut = fleets_[f->id];
        if (mut.order.kind != FleetOrderKind::Move) continue;
        const double speed = report(mut.id).speed;
        if (speed <= 0.0) continue;
        const double dx = mut.order.target_x - mut.x;
        const double dy = mut.order.target_y - mut.y;
        const double dist = std::sqrt(dx * dx + dy * dy);
        const double step = speed * elapsed_days;
        if (step >= dist) {
            mut.x = mut.order.target_x;
            mut.y = mut.order.target_y;
            mut.order = FleetOrder{};
        } else {
            mut.x += dx / dist * step;
            mut.y += dy / dist * step;
        }
    }
}

EngagementResult WarfareModel::resolve(std::uint64_t a, std::uint64_t b,
                                       double elapsed_days) {
    EngagementResult result;
    result.elapsed_days = elapsed_days;
    auto fa = fleets_.find(a), fb = fleets_.find(b);
    if (fa == fleets_.end() || fb == fleets_.end() || elapsed_days <= 0.0) {
        return result;
    }
    fa->second.engaged = fb->second.engaged = true;

    const double dps_a = report(a).attack;
    const double dps_b = report(b).attack;

    // Apply each side's damage to the other's hull pool, distributed
    // across cohorts proportional to hull share, net of per-ship
    // defense (defense absorbs a fraction of incoming damage).
    const auto apply = [this, elapsed_days](std::uint64_t fleet_id,
                                            double incoming_dps,
                                            double* ships_lost) {
        auto& list = cohorts_[fleet_id];
        double total_hull = 0.0;
        for (const auto& c : list) {
            if (const auto* cls = ship_class(c.ship_class))
                total_hull += cohort_hull(c, *cls);
        }
        if (total_hull <= 0.0) return;
        for (auto& c : list) {
            const auto* cls = ship_class(c.ship_class);
            if (!cls || c.count <= 0.0) continue;
            const double share = cohort_hull(c, *cls) / total_hull;
            const double gross = incoming_dps * share * elapsed_days;
            // Per-ship defense flatly absorbs damage before hull loss.
            const double absorb =
                cls->defense * c.count * c.condition * elapsed_days;
            const double damage = std::max(0.0, gross - absorb);
            const double lost = std::min(c.count, damage / cls->hull);
            c.count -= lost;
            *ships_lost += lost;
            // Damage thins the cohort; condition degrades with losses.
            if (c.count > 0.0 && lost > 0.0) {
                const double frac = lost / (c.count + lost);
                c.condition = std::max(0.1, c.condition * (1.0 - frac));
            }
        }
    };

    apply(b, dps_a, &result.b_ships_lost);
    apply(a, dps_b, &result.a_ships_lost);

    result.a_destroyed = report(a).ships <= 0.0;
    result.b_destroyed = report(b).ships <= 0.0;
    if (result.a_destroyed) fa->second.engaged = false;
    if (result.b_destroyed) fb->second.engaged = false;
    return result;
}

} // namespace stellar::engine
