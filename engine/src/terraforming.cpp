#include <stellar/engine/terraforming.hpp>

#include <algorithm>
#include <stdexcept>

namespace stellar::engine {

namespace {

void add_tag(std::vector<std::string>& tags, const std::string& t) {
    auto it = std::lower_bound(tags.begin(), tags.end(), t);
    if (it == tags.end() || *it != t) tags.insert(it, t);
}

void remove_tag(std::vector<std::string>& tags, const std::string& t) {
    auto it = std::lower_bound(tags.begin(), tags.end(), t);
    if (it != tags.end() && *it == t) tags.erase(it);
}

} // namespace

void Terraforming::define_project(TerraformProject project) {
    if (project.id.empty() || project.stages.empty()) {
        throw std::invalid_argument("terraform project needs id + stages");
    }
    for (const auto& s : project.stages) {
        if (s.duration_days < 0.0) {
            throw std::invalid_argument("negative stage duration");
        }
    }
    if (!projects_.emplace(project.id, std::move(project)).second) {
        throw std::invalid_argument("duplicate terraform project");
    }
}

const TerraformProject* Terraforming::project(std::string_view id) const {
    auto it = projects_.find(std::string(id));
    return it == projects_.end() ? nullptr : &it->second;
}

void Terraforming::set_environment(PlanetEnvironment env) {
    std::sort(env.tags.begin(), env.tags.end());
    env.tags.erase(std::unique(env.tags.begin(), env.tags.end()),
                   env.tags.end());
    env_ = std::move(env);
}

bool Terraforming::start(std::string_view project_id) {
    if (!active_id_.empty()) return false;
    const auto it = projects_.find(std::string(project_id));
    if (it == projects_.end()) return false;
    active_id_ = it->first;
    stage_index_ = 0;
    stage_elapsed_ = 0.0;
    return true;
}

void Terraforming::cancel() {
    active_id_.clear();
    stage_index_ = 0;
    stage_elapsed_ = 0.0;
}

double Terraforming::stage_progress() const {
    const auto* p = project(active_id_);
    if (!p || stage_index_ >= p->stages.size()) return 0.0;
    const double dur = p->stages[stage_index_].duration_days;
    if (dur <= 0.0) return 1.0;
    return std::clamp(stage_elapsed_ / dur, 0.0, 1.0);
}

double Terraforming::project_progress() const {
    const auto* p = project(active_id_);
    if (!p) return 0.0;
    double done = 0.0, total = 0.0;
    for (std::size_t i = 0; i < p->stages.size(); ++i) {
        const double d = p->stages[i].duration_days;
        total += d;
        if (i < stage_index_) {
            done += d;
        } else if (i == stage_index_) {
            done += std::min(d, stage_elapsed_);
        }
    }
    return total > 0.0 ? std::clamp(done / total, 0.0, 1.0) : 1.0;
}

TerraformAdvance Terraforming::advance(double elapsed_days) {
    TerraformAdvance result;
    result.elapsed_days = elapsed_days;
    const auto* p = project(active_id_);
    if (!p || elapsed_days <= 0.0) return result;
    result.project_id = p->id;

    double remaining = elapsed_days;
    while (remaining > 0.0 && stage_index_ < p->stages.size()) {
        const TerraformStage& stage = p->stages[stage_index_];
        const double left = stage.duration_days - stage_elapsed_;
        const double step = std::min(remaining, std::max(left, 0.0));
        if (stage.duration_days > 0.0) {
            const double f = step / stage.duration_days;
            env_.temperature_k += stage.temperature_delta_k * f;
            env_.atmosphere_atm += stage.atmosphere_delta * f;
            env_.water_fraction = std::clamp(
                env_.water_fraction + stage.water_delta * f, 0.0, 1.0);
            env_.gravity_g += stage.gravity_delta * f;
            stage_elapsed_ += step;
            remaining -= step;
        } else {
            stage_elapsed_ += 0.0;
            remaining -= step; // step == 0; completes below
        }

        if (stage_elapsed_ >= stage.duration_days) {
            for (const auto& t : stage.add_tags) add_tag(env_.tags, t);
            for (const auto& t : stage.remove_tags) remove_tag(env_.tags, t);
            result.stages_completed.push_back(stage.id);
            ++stage_index_;
            stage_elapsed_ = 0.0;
            if (stage_index_ >= p->stages.size()) {
                result.project_completed = true;
                active_id_.clear();
            }
        }
        if (step <= 0.0 && stage_index_ < p->stages.size() &&
            p->stages[stage_index_].duration_days > 0.0) {
            break; // safety: no progress possible
        }
    }
    return result;
}

} // namespace stellar::engine
