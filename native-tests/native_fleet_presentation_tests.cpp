#include "native_fleet_presentation.hpp"
#include "native_military_messages.hpp"
#include "native_route_messages.hpp"

#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::native_fleet;
using namespace stellar::native_fleet_ui;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] NativeOwnFleet fleet(int id, double x, double y) {
  NativeOwnFleet result;
  result.id = id;
  result.position = {static_cast<float>(x), static_cast<float>(y)};
  return result;
}

[[nodiscard]] bool same_offsets(std::span<const FleetMarkerOffset> left,
                                std::span<const FleetMarkerOffset> right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (left[index].fleet_id != right[index].fleet_id ||
        left[index].pixels.x != right[index].pixels.x ||
        left[index].pixels.y != right[index].pixels.y)
      return false;
  return true;
}
} // namespace

int main() try {
  const std::array systems{
      ObservedSystemName{1, "Sol", false},
      ObservedSystemName{2, "Solaris", true},
      ObservedSystemName{3, "Secret Junction", false},
      ObservedSystemName{4, "Barnard's Star", true},
      ObservedSystemName{5, "éèæ", false}};
  const auto accepted = observer_safe_fleet_message(
      "Route accepted for Sol through Solaris to Barnard's Star.", systems);
  require(accepted ==
              "Route accepted for Unknown system through Solaris to "
              "Barnard's Star.",
          "Unknown target redaction damaged a longer known shared prefix.");
  const auto denied = observer_safe_fleet_message(
      "Fuel is insufficient at Secret Junction; Secret Junction is beyond "
      "range, while Solaris remains reachable.",
      systems);
  require(denied ==
              "Fuel is insufficient at Unknown system; Unknown system is "
              "beyond range, while Solaris remains reachable.",
          "Unknown intermediate redaction missed repetition or a known name.");
  NativeFleetRoutePreview canonical_preview;
  canonical_preview.message = "Travel to Sol via Secret Junction.";
  auto ui_preview = canonical_preview;
  ui_preview.message = observer_safe_fleet_message(ui_preview.message, systems);
  require(canonical_preview.message == "Travel to Sol via Secret Junction." &&
              ui_preview.message ==
                  "Travel to Unknown system via Unknown system.",
          "Sanitizing the UI preview altered the raw canonical route command.");
  const auto boundaries = observer_safe_fleet_message(
      "Solar probes are not Sol, and éèæ is hidden.", systems);
  require(boundaries ==
              "Solar probes are not Unknown system, and Unknown system is "
              "hidden.",
          "System-name redaction ignored token boundaries or UTF-8 names.");

  stellar::engine::LocalizationTable german("de", "en");
  std::string locale_error;
  require(german.load_json(R"json({"locale":"de","strings":{
      "MIL_ORDER_DEFENDING":"{0} verteidigt System {1}.",
      "MIL_ORDER_UNKNOWN":"Unbekannter Militärbefehl."
    }})json",
                            &locale_error),
          "German military locale failed to load.");
  require(stellar::native_military::localized_message(
              &german, "Pathfinder One is defending system 0.") ==
              "Pathfinder One verteidigt System 0.",
          "German military order result kept the English skeleton.");
  require(stellar::native_military::localized_message(&german,
                                             "Unknown military order.") ==
              "Unbekannter Militärbefehl.",
          "German static military message did not translate.");
  require(stellar::native_military::localized_message(
              &german, "Pathfinder One is attacking Vanguard.") ==
              "Pathfinder One is attacking Vanguard.",
          "Unkeyed German military skeleton should fall back to English.");
  require(stellar::native_military::localized_message(
              nullptr, "Pathfinder One is defending system 0.") ==
              "Pathfinder One is defending system 0.",
          "Missing locale altered an authoritative military message.");
  require(stellar::native_military::localized_message(&german,
                                             "Free-form status text.") ==
              "Free-form status text.",
          "Unmapped military message was rewritten.");

  stellar::engine::LocalizationTable german_routes("de", "en");
  require(german_routes.load_json(R"json({"locale":"de","strings":{
      "ROUTE_EXPLORATION_APPROVED_RECON":"{0}: Aufklärungsmission für {1} genehmigt. {2}",
      "ROUTE_EXPLORATION_COURSE_SET":"{0}: Kurs auf {1} gesetzt. {2}",
      "ROUTE_CIV_RETURNING":"{0} kehrt nach {1} zurück. {2}",
      "ROUTE_MIL_DEPLOYING":"{0} wird nach {1} verlegt. {2}",
      "ROUTE_FREIGHT_SELECT":"Zuerst einen eigenen Frachter auswählen.",
      "SETTLE_REACH_ROUTE_ONE":"Route: {0} Korridoretappe, {1} gesamt; maximale Etappe {2}; voraussichtliche Treibstoffreserve {3}."
    }})json",
                            &locale_error),
          "German route locale failed to load.");
  require(stellar::native_route::localized_message(
              &german_routes,
              "Pathfinder One: reconnaissance mission approved for Proxima "
              "Centauri. Route: 1 lane leg, 3,999 × 10¹³ km total; maximum "
              "leg 4.2 ly; projected fuel reserve 8.0 ly.") ==
              "Pathfinder One: Aufklärungsmission für Proxima Centauri "
              "genehmigt. Route: 1 Korridoretappe, 3,999 × 10¹³ km gesamt; "
              "maximale Etappe 4.2 ly; voraussichtliche Treibstoffreserve "
              "8.0 ly.",
          "German exploration approval kept the English skeleton.");
  require(stellar::native_route::localized_message(
              &german_routes,
              "Pathfinder One is returning to Sol. Route: 1 lane leg, 4.0 "
              "ly total; maximum leg 4.2 ly; projected fuel reserve 8.0 ly.") ==
              "Pathfinder One kehrt nach Sol zurück. Route: 1 Korridoretappe, "
              "4.0 ly gesamt; maximale Etappe 4.2 ly; voraussichtliche "
              "Treibstoffreserve 8.0 ly.",
          "German civilian return kept the English skeleton.");
  require(stellar::native_route::localized_message(
              &german_routes, "Select an owned freighter first.") ==
              "Zuerst einen eigenen Frachter auswählen.",
          "German static route message did not translate.");
  require(stellar::native_route::localized_message(
              &german_routes,
              "Vanguard: science-survey mission approved for Tau Ceti. The "
              "fleet is already in the target system.") ==
              "Vanguard: science-survey mission approved for Tau Ceti. The "
              "fleet is already in the target system.",
          "Unkeyed German route skeleton should fall back to English.");
  require(stellar::native_route::localized_message(
              nullptr,
              "Pathfinder One: reconnaissance mission approved for Proxima "
              "Centauri. Route: 1 lane leg, 4.0 ly total; maximum leg 4.2 ly; "
              "projected fuel reserve 8.0 ly.") ==
              "Pathfinder One: reconnaissance mission approved for Proxima "
              "Centauri. Route: 1 lane leg, 4.0 ly total; maximum leg 4.2 ly; "
              "projected fuel reserve 8.0 ly.",
          "Missing locale altered an authoritative route message.");

  std::vector<NativeOwnFleet> first{
      fleet(12, 4, 7), fleet(10, 4, 7), fleet(14, 9, 3), fleet(11, 4, 7)};
  auto second = first;
  std::ranges::reverse(second);
  const auto offsets = deterministic_fleet_marker_offsets(first);
  const auto reversed = deterministic_fleet_marker_offsets(second);
  require(same_offsets(offsets, reversed) && offsets.size() == 4 &&
              std::ranges::is_sorted(offsets, {}, &FleetMarkerOffset::fleet_id),
          "Fleet marker offsets changed with input ordering.");
  require(offsets[0].fleet_id == 10 && offsets[0].pixels.x == 0.f &&
              offsets[0].pixels.y == 0.f &&
              std::hypot(offsets[1].pixels.x, offsets[1].pixels.y) > 0.f &&
              std::hypot(offsets[2].pixels.x, offsets[2].pixels.y) > 0.f &&
              offsets[3].fleet_id == 14 && offsets[3].pixels.x == 0.f &&
              offsets[3].pixels.y == 0.f,
          "Co-located fleets did not receive stable distinct offsets.");

  std::cout << "Native fleet observer-safe messages and stable grouped marker "
               "offsets passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
