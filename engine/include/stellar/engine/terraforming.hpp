#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <stellar/engine/planetary.hpp>

namespace stellar::engine {

// Terraforming — staged, physical mutation of a PlanetEnvironment.
// NOT a universal progress bar: each stage applies real environment
// deltas (temperature, atmosphere, water, gravity) linearly across its
// duration and discrete tag changes at completion. Habitability is
// re-evaluated by callers via evaluate_habitability — species-relative,
// never absolute.
//
// One Terraforming instance per planet: it owns the adapted
// PlanetEnvironment (the adapter writes it via set_environment and
// re-reads it back to authoritative state). Deterministic: linear
// interpolation, discrete tag application at stage boundaries, no RNG.

struct TerraformStage {
    std::string id;
    double duration_days{0.0};
    // Linear deltas spread across the stage duration:
    double temperature_delta_k{0.0};
    double atmosphere_delta{0.0};
    double water_delta{0.0};
    double gravity_delta{0.0};
    // Discrete changes applied at stage completion:
    std::vector<std::string> add_tags;
    std::vector<std::string> remove_tags;
};

struct TerraformProject {
    std::string id;
    std::vector<TerraformStage> stages; // sequential
};

struct TerraformAdvance {
    double elapsed_days{0.0};
    std::vector<std::string> stages_completed;   // stage ids, in order
    bool project_completed{false};
    std::string project_id;
};

class Terraforming {
public:
    void define_project(TerraformProject project); // duplicate id throws
    [[nodiscard]] const TerraformProject* project(std::string_view id) const;

    // Adapter-owned environment: write the projected state, read it
    // back to push into authoritative Core state.
    void set_environment(PlanetEnvironment env);
    [[nodiscard]] const PlanetEnvironment& environment() const { return env_; }
    [[nodiscard]] PlanetEnvironment& environment() { return env_; }

    // Starts a project — false if unknown id or one is already active.
    bool start(std::string_view project_id);
    // Stops work; deltas already applied persist (physical work done is
    // not undone). The project may be restarted from stage 0 later.
    void cancel();
    [[nodiscard]] bool active() const { return !active_id_.empty(); }
    [[nodiscard]] const std::string& active_project() const { return active_id_; }
    [[nodiscard]] std::size_t stage_index() const { return stage_index_; }
    // 0..1 within current stage and across the whole project.
    [[nodiscard]] double stage_progress() const;
    [[nodiscard]] double project_progress() const;

    // Apply deltas for elapsed days; completes stages/projects whose
    // durations expire mid-step (multiple completions possible).
    // Inactive or finished → no-op result.
    TerraformAdvance advance(double elapsed_days);

private:
    PlanetEnvironment env_;
    std::unordered_map<std::string, TerraformProject> projects_;
    std::string active_id_;
    std::size_t stage_index_{0};
    double stage_elapsed_{0.0};
};

} // namespace stellar::engine
