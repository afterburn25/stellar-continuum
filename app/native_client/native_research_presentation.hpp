#pragma once

#include <array>
#include <string>
#include <string_view>
#include <stellar/engine/localization.hpp>

namespace stellar::native_research {

[[nodiscard]] inline std::string research_tr(
    const stellar::engine::LocalizationTable *locale, std::string_view key,
    std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

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
                                    std::string_view family,
                                    const stellar::engine::LocalizationTable *locale = nullptr) {
  if (id == "in_space_assembly") return research_tr(locale,"RESEARCH_PURPOSE_SPACE_ASSEMBLY","Develops methods for assembling and validating orbital structures.");
  if (id == "asteroid_prospecting") return research_tr(locale,"RESEARCH_PURPOSE_PROSPECTING","Develops ways to locate and assess resource deposits for later mining.");
  if (id == "asteroid_mining") return research_tr(locale,"RESEARCH_PURPOSE_ASTEROID_MINING","Develops controlled extraction methods for accessible asteroid material.");
  if (id == "vacuum_refining") return research_tr(locale,"RESEARCH_PURPOSE_VACUUM_REFINING","Studies processing and purification methods that work in vacuum.");
  if (id == "orbital_manufacturing") return research_tr(locale,"RESEARCH_PURPOSE_ORBITAL_MANUFACTURING","Develops repeatable manufacturing methods for orbital industry.");
  if (id == "orbital_shipyard") return research_tr(locale,"RESEARCH_PURPOSE_ORBITAL_SHIPYARD","Develops the engineering basis for an orbital shipyard.");
  if (id == "gravitational_physics") return research_tr(locale,"RESEARCH_PURPOSE_GRAVITY","Builds methods to measure gravity for advanced propulsion research.");
  if (id == "field_theory") return research_tr(locale,"RESEARCH_PURPOSE_FIELD_THEORY","Develops mathematical and experimental tools for controllable field research.");
  if (id == "warp_metric_theory") return research_tr(locale,"RESEARCH_PURPOSE_WARP_METRIC","Investigates metric models relevant to spacetime-field research.");
  if (id == "exotic_energy_coupling") return research_tr(locale,"RESEARCH_PURPOSE_EXOTIC_COUPLING","Investigates how exotic energy sources could couple to controlled fields.");
  if (id == "micro_field_distortion") return research_tr(locale,"RESEARCH_PURPOSE_MICRO_DISTORTION","Tests small-scale controlled distortion experiments.");
  if (id == "warp_field_control") return research_tr(locale,"RESEARCH_PURPOSE_WARP_CONTROL","Develops control methods for stable laboratory-scale distortion fields.");
  if (id == "prototype_warp_drive") return research_tr(locale,"RESEARCH_PURPOSE_WARP_DRIVE","Develops a vessel-scale prototype for interstellar transit.");
  if (locale && locale->contains("RESEARCH_PURPOSE_GENERIC")) {
    const std::string args[]{std::string(domain), std::string(family)};
    return locale->format("RESEARCH_PURPOSE_GENERIC", std::span<const std::string>(args));
  }
  return "Explores a practical " + std::string(domain) + " approach in the " + std::string(family) + " field.";
}

inline std::string research_benefit(std::string_view id,
                                    const stellar::engine::LocalizationTable *locale = nullptr) {
  if (id == "orbital_manufacturing") return research_tr(locale,"RESEARCH_BENEFIT_ORBITAL_MANUFACTURING","At mature completion, Orbital Industry enables spacecraft construction and makes the Orbital Shipyard project available once the Orbital Launch Complex is complete.");
  if (id == "orbital_shipyard") return research_tr(locale,"RESEARCH_BENEFIT_ORBITAL_SHIPYARD","Improves the knowledge base for operating orbital yards. Building a yard still requires its separate construction project and launch-complex prerequisite.");
  if (id == "prototype_warp_drive") return research_tr(locale,"RESEARCH_BENEFIT_WARP_DRIVE","A demonstrated result supports limited interstellar transit. Ships still require an operational Orbital Shipyard.");
  if (id == "in_space_assembly") return research_tr(locale,"RESEARCH_BENEFIT_SPACE_ASSEMBLY","Provides assembly and validation methods needed before large orbital manufacturing can be attempted.");
  if (id == "asteroid_prospecting") return research_tr(locale,"RESEARCH_BENEFIT_PROSPECTING","Improves the basis for selecting targets before an extraction program is proposed.");
  if (id == "asteroid_mining") return research_tr(locale,"RESEARCH_BENEFIT_ASTEROID_MINING","Provides extraction methods needed before asteroid-material operations can be developed.");
  if (id == "vacuum_refining") return research_tr(locale,"RESEARCH_BENEFIT_VACUUM_REFINING","Provides vacuum-processing knowledge needed for orbital manufacturing work.");
  return research_tr(locale,"RESEARCH_BENEFIT_GENERIC","Contributes knowledge for the next practical step in this field. Research alone does not construct facilities or grant an immediate production bonus.");
}
} // namespace stellar::native_research
