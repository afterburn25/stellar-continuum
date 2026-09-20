#pragma once

#include <array>
#include <string>
#include <string_view>

namespace stellar::native_research {

// Presentation categories from the preserved ResearchWorkspaceView. Filtering
// operates only on the observer's projected nodes, never on the hidden catalog.
inline constexpr std::array research_categories{
    std::pair{"physics", "Physics"}, std::pair{"engineering", "Engineering"},
    std::pair{"energy", "Energy"}, std::pair{"computing", "Computing"},
    std::pair{"materials", "Materials"}, std::pair{"biology", "Biology & Medicine"},
    std::pair{"society", "Society & Economy"}, std::pair{"xenoscience", "Xenoscience"}};

inline std::string_view research_category(std::string_view domain) noexcept {
  if (domain == "foundations" || domain == "research_infrastructure") return "physics";
  if (domain == "propulsion" || domain == "space_industry" || domain == "planetary" ||
      domain == "military" || domain == "logistics" || domain == "stellar_engineering") return "engineering";
  if (domain == "sensors_comms" || domain == "synthetic_systems" || domain == "cybernetics") return "computing";
  if (domain == "life_medicine" || domain == "biotechnology" || domain == "biosphere_agriculture" ||
      domain == "alternative_biochemistry") return "biology";
  if (domain == "social_admin" || domain == "economic_trade") return "society";
  return domain;
}

inline bool research_category_matches(std::string_view category, std::string_view domain) noexcept {
  return category == domain || category == research_category(domain);
}

// Ported player-facing explanations from Main.ResearchExplanation.cs. Callers
// must first establish that the program is known to the current observer.
inline std::string research_purpose(std::string_view id, std::string_view domain,
                                    std::string_view family) {
  if (id == "in_space_assembly") return "Develops methods for assembling and validating orbital structures.";
  if (id == "asteroid_prospecting") return "Develops ways to locate and assess resource deposits for later mining.";
  if (id == "asteroid_mining") return "Develops controlled extraction methods for accessible asteroid material.";
  if (id == "vacuum_refining") return "Studies processing and purification methods that work in vacuum.";
  if (id == "orbital_manufacturing") return "Develops repeatable manufacturing methods for orbital industry.";
  if (id == "orbital_shipyard") return "Develops the engineering basis for an orbital shipyard.";
  if (id == "gravitational_physics") return "Builds methods to measure gravity for advanced propulsion research.";
  if (id == "field_theory") return "Develops mathematical and experimental tools for controllable field research.";
  if (id == "warp_metric_theory") return "Investigates metric models relevant to spacetime-field research.";
  if (id == "exotic_energy_coupling") return "Investigates how exotic energy sources could couple to controlled fields.";
  if (id == "micro_field_distortion") return "Tests small-scale controlled distortion experiments.";
  if (id == "warp_field_control") return "Develops control methods for stable laboratory-scale distortion fields.";
  if (id == "prototype_warp_drive") return "Develops a vessel-scale prototype for interstellar transit.";
  return "Explores a practical " + std::string(domain) + " approach in the " + std::string(family) + " field.";
}

inline std::string research_benefit(std::string_view id) {
  if (id == "orbital_manufacturing") return "At mature completion, Orbital Industry enables spacecraft construction and makes the Orbital Shipyard project available once the Orbital Launch Complex is complete.";
  if (id == "orbital_shipyard") return "Improves the knowledge base for operating orbital yards. Building a yard still requires its separate construction project and launch-complex prerequisite.";
  if (id == "prototype_warp_drive") return "A demonstrated result supports limited interstellar transit. Ships still require an operational Orbital Shipyard.";
  if (id == "in_space_assembly") return "Provides assembly and validation methods needed before large orbital manufacturing can be attempted.";
  if (id == "asteroid_prospecting") return "Improves the basis for selecting targets before an extraction program is proposed.";
  if (id == "asteroid_mining") return "Provides extraction methods needed before asteroid-material operations can be developed.";
  if (id == "vacuum_refining") return "Provides vacuum-processing knowledge needed for orbital manufacturing work.";
  return "Contributes knowledge for the next practical step in this field. Research alone does not construct facilities or grant an immediate production bonus.";
}
} // namespace stellar::native_research
