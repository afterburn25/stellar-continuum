#include <stellar/core/detail/civilization_founding_roster.hpp>

#include <string>

namespace stellar::core::detail {

std::vector<CivilizationOffice>
civilization_founding_roster(int civilization_id, bool human) {
  struct Office {
    const char *office;
    const char *human_name;
    const char *other_name;
  };
  static constexpr Office offices[] = {
      {"FleetCommander", "Commander Elena Voss", "Fleet Commander"},
      {"ChiefScientist", "Dr. Amara Chen", "Chief Scientist"},
      {"Diplomat", "Ambassador Mara Okafor", "Diplomatic Envoy"},
      {"Governor", "Governor Elias Ward", "Governor"},
      {"EconomicAdvisor", "Economic Advisor", "Economic Advisor"},
      {"OperationsOfficer", "Operations Officer", "Operations Officer"},
      {"ExpeditionCommander", "Expedition Commander", "Expedition Commander"}};
  std::vector<CivilizationOffice> result;
  result.reserve(std::size(offices));
  for (const auto &entry : offices)
    result.push_back({entry.office,
                      {"civ-" + std::to_string(civilization_id) + ":" +
                           entry.office + ":founder",
                       human ? entry.human_name : entry.other_name,
                       {},
                       {}}});
  return result;
}

} // namespace stellar::core::detail