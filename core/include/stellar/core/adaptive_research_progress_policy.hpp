#pragma once

#include <stellar/core/adaptive_research_catalog.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace stellar::core {

struct ResearchStageWorkBand {
  ResearchMaturity stage{};
  double start_fraction{};
  double end_fraction{};
  [[nodiscard]] double work_fraction() const noexcept { return end_fraction - start_fraction; }
};

struct ResearchReadinessEfficiencyBand { double minimum_score{}; double maximum_score{}; double efficiency{}; };

class AdaptiveResearchProgressPolicyError final : public std::runtime_error {
public:
  explicit AdaptiveResearchProgressPolicyError(std::string message);
};

class AdaptiveResearchProgressPolicy final {
public:
  AdaptiveResearchProgressPolicy(const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchProgressPolicy &operator=(const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchProgressPolicy(AdaptiveResearchProgressPolicy &&) noexcept;
  AdaptiveResearchProgressPolicy &operator=(AdaptiveResearchProgressPolicy &&) noexcept;
  ~AdaptiveResearchProgressPolicy();

  [[nodiscard]] std::span<const ResearchStageWorkBand> stage_bands() const noexcept;
  [[nodiscard]] std::span<const ResearchReadinessEfficiencyBand> readiness_bands() const noexcept;
  [[nodiscard]] const ResearchStageWorkBand &get_stage_band(ResearchMaturity stage) const;
  [[nodiscard]] double get_stage_work(const AdaptiveResearchNodeDefinition &node,
                                      ResearchMaturity stage) const;
  [[nodiscard]] double get_readiness_efficiency(double readiness_score) const;

private:
  struct Storage;
  explicit AdaptiveResearchProgressPolicy(Storage storage);
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchProgressPolicy load_adaptive_research_progress_policy(
      const std::filesystem::path &, const AdaptiveResearchCatalog &);
};

[[nodiscard]] AdaptiveResearchProgressPolicy load_adaptive_research_progress_policy(
    const std::filesystem::path &root_path, const AdaptiveResearchCatalog &catalog);

} // namespace stellar::core
