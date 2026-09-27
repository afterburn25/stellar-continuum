#include "native_fleet_presentation.hpp"
#include "native_military_messages.hpp"
#include "native_research_messages.hpp"
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
      "ROUTE_FREIGHT_ASSIGNED":"{0} ist bereits einem Frachtlauf zugewiesen.",
      "ROUTE_FREIGHT_OUTPOST":"Dieser bemannte Ressourcen-Außenposten ist nicht verfügbar.",
      "ROUTE_FREIGHT_DISPATCHED":"{0} entsendet, um bis zu {1} Materialeinheiten von {2} abzuholen. {3}",
      "ROUTE_FREIGHT_OP_RUNNING":"Gewinnung läuft mit {0} % Betriebsfinanzierung",
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
  require(stellar::native_route::localized_message(
              &german_routes,
              "Merchant One is already assigned to a freight run.") ==
              "Merchant One ist bereits einem Frachtlauf zugewiesen.",
          "German freight assignment kept the English skeleton.");
  require(stellar::native_route::localized_message(
              &german_routes,
              "That staffed resource outpost is unavailable.") ==
              "Dieser bemannte Ressourcen-Außenposten ist nicht verfügbar.",
          "German freight static did not translate.");
  require(stellar::native_route::localized_message(
              &german_routes,
              "Extraction running at 42% operating funding") ==
              "Gewinnung läuft mit 42 % Betriebsfinanzierung",
          "German extraction status kept the English skeleton.");
  require(stellar::native_route::localized_message(
              &german_routes,
              "Merchant One dispatched to collect up to 12 material units "
              "from Depot Alpha. Route: 1 lane leg, 4.0 ly total; maximum "
              "leg 4.2 ly; projected fuel reserve 8.0 ly.") ==
              "Merchant One entsendet, um bis zu 12 Materialeinheiten von "
              "Depot Alpha abzuholen. Route: 1 Korridoretappe, 4.0 ly gesamt; "
              "maximale Etappe 4.2 ly; voraussichtliche Treibstoffreserve "
              "8.0 ly.",
          "German freight dispatch kept the English skeleton.");

  stellar::engine::LocalizationTable german_research("de", "en");
  require(german_research.load_json(R"json({"locale":"de","strings":{
      "RESEARCH_BLOCK_PREREQ":"Zusätzliches Voraussetzungswissen ist erforderlich.",
      "RESEARCH_MSG_RESUMED":"Forschungsprojekt fortgesetzt.",
      "RESEARCH_MSG_STARTED_FUNDING":"Gelenkte Forschung gestartet: {0}. Autorisiert für {1}; {2} für Prototypen und Validierung reserviert; geplante Betriebskosten {3}.",
      "RESEARCH_MSG_CANCELLED_REFUND":"Forschung abgebrochen. {0} erstattet.",
      "RESEARCH_MSG_DISPROVEN":"{0} wurde widerlegt."
    }})json",
                            &locale_error),
          "German research locale failed to load.");
  require(stellar::native_research::localized_message(
              &german_research,
              "Additional prerequisite knowledge is required.") ==
              "Zusätzliches Voraussetzungswissen ist erforderlich.",
          "German research blocker did not translate.");
  require(stellar::native_research::localized_message(
              &german_research, "Research project resumed.") ==
              "Forschungsprojekt fortgesetzt.",
          "German research result did not translate.");
  require(stellar::native_research::localized_message(
              &german_research,
              "Directed research started: Fusion Drive. Authorized for "
              "₡120.00; ₡300.00 reserved for prototypes and validation; "
              "planned operations cost ₡4.00/day.") ==
              "Gelenkte Forschung gestartet: Fusion Drive. Autorisiert für "
              "₡120.00; ₡300.00 für Prototypen und Validierung reserviert; "
              "geplante Betriebskosten ₡4.00/day.",
          "German funded research start kept the English skeleton.");
  require(stellar::native_research::localized_message(
              &german_research,
              "Research cancelled; laboratories released and scientific work "
              "preserved. Refunded ₡300.00 in unused milestone funds. "
              "Authorization and operating costs are not refundable. "
              "Restarting requires new authorization and milestone "
              "funding.") ==
              "Forschung abgebrochen. ₡300.00 erstattet.",
          "German research refund kept the English skeleton.");
  require(stellar::native_research::localized_message(
              &german_research,
              "Fusion Drive was disproven; accumulated negative knowledge is "
              "preserved.") ==
              "Fusion Drive wurde widerlegt.",
          "German disproval kept the English skeleton.");
  require(stellar::native_research::localized_message(
              nullptr, "Research project resumed.") ==
              "Research project resumed.",
          "Missing locale altered an authoritative research message.");
  require(stellar::native_research::localized_message(
              &german_research, "Free-form research note.") ==
              "Free-form research note.",
          "Unmapped research message was rewritten.");

  stellar::engine::LocalizationTable german_tactical("de", "en");
  require(german_tactical.load_json(R"json({"locale":"de","strings":{
      "MIL_TACTICAL_NO_HOSTILE":"Keine angreifbare feindliche Formation in diesem System entdeckt.",
      "MIL_TACTICAL_ESTABLISHED":"Gefecht etabliert: {0} kampfbereite Schiffe. Taktische Befehle bereit.",
      "MIL_TACTICAL_ACK_ORDER":"{0} bestätigt {1}.",
      "BATTLE_ORDER_STANDOFF":"Standoff-Angriff"
    }})json",
                            &locale_error),
          "German tactical locale failed to load.");
  require(stellar::native_military::localized_message(
              &german_tactical,
              "No attackable hostile formation is detected in this system.") ==
              "Keine angreifbare feindliche Formation in diesem System "
              "entdeckt.",
          "German tactical static did not translate.");
  require(stellar::native_military::localized_message(
              &german_tactical,
              "Encounter established: 5 commissioned vessels. Tactical "
              "orders ready.") ==
              "Gefecht etabliert: 5 kampfbereite Schiffe. Taktische Befehle "
              "bereit.",
          "German engagement result kept the English skeleton.");
  require(stellar::native_military::localized_message(
              &german_tactical,
              "Vanguard acknowledged StandoffAttack.") ==
              "Vanguard bestätigt Standoff-Angriff.",
          "German order acknowledgment kept the English order name.");
  require(stellar::native_military::localized_message(
              &german_tactical, "Vanguard acknowledged retreat.") ==
              "Vanguard acknowledged retreat.",
          "Unmapped order name should fall back to English.");

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
