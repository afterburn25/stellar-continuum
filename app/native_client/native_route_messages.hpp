#pragma once
#include <stellar/engine/localization.hpp>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "native_settlement_messages.hpp"
namespace stellar::native_route {
// Order results and previews composed by core exploration, military
// deployment, freight transit, colony transit and civilian-recovery paths are
// stable English literals or fixed skeletons with dynamic fragments. Recompose
// them through the locale table at the display boundary so fleet-workspace
// notices stay localized without changing the authoritative messages or saves.
[[nodiscard]] inline std::string localized_message(
    const stellar::engine::LocalizationTable *locale, std::string_view message) {
  if (!locale || message.empty()) return std::string(message);
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return locale->contains(key) ? std::string(locale->translate(key))
                                 : std::string(fallback);
  };
  const auto trf =
      [&](std::string_view key, std::initializer_list<std::string> args,
          std::string_view fallback) {
        if (locale->contains(key)) {
          const std::vector<std::string> values(args.begin(), args.end());
          return locale->format(key, std::span<const std::string>(values));
        }
        std::string out{fallback};
        std::size_t index = 0;
        for (const auto &arg : args) {
          const std::string marker = "{" + std::to_string(index++) + "}";
          if (const auto at = out.find(marker); at != std::string::npos)
            out.replace(at, marker.size(), arg);
        }
        return out;
      };
  const auto divide = [](std::string_view text, std::string_view infix)
      -> std::optional<std::pair<std::string_view, std::string_view>> {
    const auto at = text.find(infix);
    if (at == std::string_view::npos) return std::nullopt;
    return std::pair{text.substr(0, at), text.substr(at + infix.size())};
  };
  const auto strip_prefix = [](std::string_view text, std::string_view prefix)
      -> std::optional<std::string_view> {
    if (!text.starts_with(prefix)) return std::nullopt;
    return text.substr(prefix.size());
  };
  const auto strip_suffix = [](std::string_view text, std::string_view suffix)
      -> std::optional<std::string_view> {
    if (!text.ends_with(suffix)) return std::nullopt;
    return text.substr(0, text.size() - suffix.size());
  };
  const auto reason = [&](std::string_view tail) {
    return stellar::native_settlement::localized_reach_reason(locale, tail);
  };
  static const std::pair<std::string_view, std::string_view> statics[] = {
      {"No active exploration vessel with that fleet ID is available.",
       "ROUTE_EXPLORATION_NO_VESSEL"},
      {"Unknown astronomical target.", "ROUTE_EXPLORATION_UNKNOWN_TARGET"},
      {"No remaining survey work is available to this vessel.",
       "ROUTE_EXPLORATION_NO_WORK"},
      {"No controllable active military fleet with that identity is "
       "available.",
       "ROUTE_MIL_NO_FLEET"},
      {"The selected deployment destination is not a valid star system.",
       "ROUTE_MIL_BAD_DEST"},
      {"Select an owned freighter first.", "ROUTE_FREIGHT_SELECT"},
      {"Finish the current cargo collection and delivery before assigning "
       "another course.",
       "ROUTE_FREIGHT_BUSY"},
      {"Select an active colony or outpost ship you control.",
       "ROUTE_COLONY_SELECT"},
      {"No controllable active civilian mission ship with that identity is "
       "available.",
       "ROUTE_CIV_NO_SHIP"},
      {"No owned refuelling settlement is reachable with the fleet's current "
       "fuel.",
       "ROUTE_CIV_NO_BASE"},
      {"No owned refuelling settlement is reachable with current fuel.",
       "ROUTE_CIV_NO_BASE_FUEL"},
      {"Finish the current lane first; return routing will then be rechecked "
       "using actual fuel.",
       "ROUTE_CIV_FINISH_LANE_FIRST"},
      {"No civilian return order is pending.", "ROUTE_CIV_NO_PENDING"},
  };
  for (const auto &[literal, key] : statics)
    if (message == literal) return tr(key, literal);
  // "{fleet} is not a scout or science survey vessel."
  if (const auto name = strip_suffix(
          message, " is not a scout or science survey vessel."))
    return trf("ROUTE_EXPLORATION_WRONG_ROLE", {std::string(*name)},
               "{0} is not a scout or science survey vessel.");
  // "{system} already has reconnaissance-grade survey coverage."
  if (const auto name = strip_suffix(
          message, " already has reconnaissance-grade survey coverage."))
    return trf("ROUTE_EXPLORATION_RECON_COVERED", {std::string(*name)},
               "{0} already has reconnaissance-grade survey coverage.");
  // "{system} already has a completed detailed science survey."
  if (const auto name = strip_suffix(
          message, " already has a completed detailed science survey."))
    return trf("ROUTE_EXPLORATION_SURVEY_COVERED", {std::string(*name)},
               "{0} already has a completed detailed science survey.");
  // "{n} survey target(s) available in the current planning window."
  if (const auto p = divide(message, " survey target"))
    if (const auto tail = strip_suffix(
            p->second, " available in the current planning window."))
      if (tail->empty() || *tail == "s")
        return trf(p->first == "1" ? "ROUTE_EXPLORATION_TARGETS_ONE"
                                   : "ROUTE_EXPLORATION_TARGETS",
                   {std::string(p->first)},
                   "{0} survey target(s) available in the current planning "
                   "window.");
  // "{fleet} is already on station in {system}."
  if (const auto p = divide(message, " is already on station in "))
    if (const auto name = strip_suffix(p->second, "."))
      return trf("ROUTE_EXPLORATION_ON_STATION",
                 {std::string(p->first), std::string(*name)},
                 "{0} is already on station in {1}.");
  // "{fleet}: course set for {system}. {reason}"
  if (const auto p = divide(message, ": course set for "))
    if (const auto q = divide(p->second, ". "))
      return trf("ROUTE_EXPLORATION_COURSE_SET",
                 {std::string(p->first), std::string(q->first),
                  reason(q->second)},
                 "{0}: course set for {1}. {2}");
  // "{fleet} is ready to begin the {action} in {system}."
  if (const auto p = divide(message, " is ready to begin the "))
    if (const auto q = divide(p->second, " in "))
      if (const auto name = strip_suffix(q->second, ".")) {
        const std::string_view key =
            q->first == "reconnaissance pass" ? "ROUTE_EXPLORATION_BEGIN_RECON"
            : q->first == "detailed science survey"
                ? "ROUTE_EXPLORATION_BEGIN_SURVEY"
                : "";
        if (!key.empty())
          return trf(key, {std::string(p->first), std::string(*name)},
                     key == "ROUTE_EXPLORATION_BEGIN_RECON"
                         ? "{0} is ready to begin the reconnaissance pass in "
                           "{1}."
                         : "{0} is ready to begin the detailed science survey "
                           "in {1}.");
      }
  // "{fleet}: {action} approved for {system}. {reason}"
  if (const auto p = divide(message, " approved for "))
    if (const auto head = divide(p->first, ": "))
      if (const auto q = divide(p->second, ". ")) {
        const std::string_view key =
            head->second == "reconnaissance mission"
                ? "ROUTE_EXPLORATION_APPROVED_RECON"
            : head->second == "science-survey mission"
                ? "ROUTE_EXPLORATION_APPROVED_SURVEY"
                : "";
        if (!key.empty())
          return trf(key,
                     {std::string(head->first), std::string(q->first),
                      reason(q->second)},
                     key == "ROUTE_EXPLORATION_APPROVED_RECON"
                         ? "{0}: reconnaissance mission approved for {1}. {2}"
                         : "{0}: science-survey mission approved for {1}. {2}");
      }
  // "{fleet} is already stationed in {system}."
  if (const auto p = divide(message, " is already stationed in "))
    if (const auto name = strip_suffix(p->second, "."))
      return trf("ROUTE_MIL_STATIONED",
                 {std::string(p->first), std::string(*name)},
                 "{0} is already stationed in {1}.");
  // "{fleet} is deploying to {system}. {reason}"
  if (const auto p = divide(message, " is deploying to "))
    if (const auto q = divide(p->second, ". "))
      return trf("ROUTE_MIL_DEPLOYING",
                 {std::string(p->first), std::string(q->first),
                  reason(q->second)},
                 "{0} is deploying to {1}. {2}");
  // "{fleet}: course set. Colonists remain aboard until you right-click a
  //  surveyed world to authorize settlement. {reason}"
  if (const auto p = divide(message, ": course set. Colonists remain aboard "
                                     "until you right-click a surveyed "
                                     "world to authorize settlement. "))
      return trf("ROUTE_COLONY_COURSE_SET",
                 {std::string(p->first), reason(p->second)},
                 "{0}: course set. Colonists remain aboard until you "
                 "right-click a surveyed world to authorize settlement. {1}");
  // "{fleet}: course set. {reason}"
  if (const auto p = divide(message, ": course set. "))
    return trf("ROUTE_FREIGHT_COURSE_SET",
               {std::string(p->first), reason(p->second)},
               "{0}: course set. {1}");
  // "Returning {fleet} will abandon its paid colony authorization with no
  //  refund. Current establishment progress: {n} days; all of it will be
  //  lost. Colonists remain aboard. Confirm return to continue."
  if (const auto rest = strip_prefix(message, "Returning "))
    if (const auto p = divide(
            *rest,
            " will abandon its paid colony authorization with no refund. "
            "Current establishment progress: "))
      if (const auto days = strip_suffix(
              p->second,
              " days; all of it will be lost. Colonists remain aboard. "
              "Confirm return to continue."))
        return trf("ROUTE_CIV_RETURN_ABANDON",
                   {std::string(p->first), std::string(*days)},
                   "Returning {0} will abandon its paid colony authorization "
                   "with no refund. Current establishment progress: {1} days; "
                   "all of it will be lost. Colonists remain aboard. Confirm "
                   "return to continue.");
  // "{fleet} must finish its current lane before return routing can be
  //  rechecked."
  if (const auto name = strip_suffix(
          message,
          " must finish its current lane before return routing can be "
          "rechecked."))
    return trf("ROUTE_CIV_MUST_FINISH_LANE", {std::string(*name)},
               "{0} must finish its current lane before return routing can "
               "be rechecked.");
  // "{fleet} is holding safely: {reason}"
  if (const auto p = divide(message, " is holding safely: "))
    return trf("ROUTE_CIV_HOLDING_SAFE",
               {std::string(p->first), reason(p->second)},
               "{0} is holding safely: {1}");
  // "{fleet} is already at the nearest owned refuelling settlement."
  if (const auto name = strip_suffix(
          message,
          " is already at the nearest owned refuelling settlement."))
    return trf("ROUTE_CIV_AT_BASE", {std::string(*name)},
               "{0} is already at the nearest owned refuelling settlement.");
  // "{fleet} is returning to {system}. {reason}"
  if (const auto p = divide(message, " is returning to "))
    if (const auto q = divide(p->second, ". "))
      return trf("ROUTE_CIV_RETURNING",
                 {std::string(p->first), std::string(q->first),
                  reason(q->second)},
                 "{0} is returning to {1}. {2}");
  // "{fleet} already has a hold order."
  if (const auto name = strip_suffix(message, " already has a hold order."))
    return trf("ROUTE_CIV_HOLD_EXISTS", {std::string(*name)},
               "{0} already has a hold order.");
  // "{fleet} is holding at {system|the current system}."
  if (const auto p = divide(message, " is holding at "))
    if (const auto name = strip_suffix(p->second, "."))
      return trf("ROUTE_CIV_HOLDING_AT",
                 {std::string(p->first),
                  *name == "the current system"
                      ? tr("ROUTE_CIV_CURRENT_SYSTEM", "the current system")
                      : std::string(*name)},
                 "{0} is holding at {1}.");
  // "{fleet} will hold after reaching {system|the next system}."
  if (const auto p = divide(message, " will hold after reaching "))
    if (const auto name = strip_suffix(p->second, "."))
      return trf("ROUTE_CIV_WILL_HOLD",
                 {std::string(p->first),
                  *name == "the next system"
                      ? tr("ROUTE_CIV_NEXT_SYSTEM", "the next system")
                      : std::string(*name)},
                 "{0} will hold after reaching {1}.");
  // "{fleet} is already proceeding under its current orders."
  if (const auto name = strip_suffix(
          message, " is already proceeding under its current orders."))
    return trf("ROUTE_CIV_PROCEEDING", {std::string(*name)},
               "{0} is already proceeding under its current orders.");
  // "{fleet} resumed its existing orders."
  if (const auto name = strip_suffix(message, " resumed its existing orders."))
    return trf("ROUTE_CIV_RESUMED", {std::string(*name)},
               "{0} resumed its existing orders.");
  // "Nearest reachable refuelling settlement: {system}. {reason}"
  if (const auto rest = strip_prefix(
          message, "Nearest reachable refuelling settlement: "))
    if (const auto q = divide(*rest, ". "))
      return trf("ROUTE_CIV_NEAREST_BASE",
                 {std::string(q->first), reason(q->second)},
                 "Nearest reachable refuelling settlement: {0}. {1}");
  // "{fleet} already has a return-to-base order."
  if (const auto name = strip_suffix(
          message, " already has a return-to-base order."))
    return trf("ROUTE_CIV_RETURN_EXISTS", {std::string(*name)},
               "{0} already has a return-to-base order.");
  // "{fleet} will finish its current lane, then re-evaluate a safe return
  //  route."
  if (const auto name = strip_suffix(
          message,
          " will finish its current lane, then re-evaluate a safe return "
          "route."))
    return trf("ROUTE_CIV_WILL_REEVAL", {std::string(*name)},
               "{0} will finish its current lane, then re-evaluate a safe "
               "return route.");
  // Reach reasons and metric skeletons stand alone as preview/denial text.
  if (const auto localized =
          stellar::native_settlement::localized_reach_reason(locale, message);
      localized != message)
    return localized;
  if (const auto localized =
          stellar::native_settlement::localized_metric(locale, message);
      localized != message)
    return localized;
  return std::string(message);
}
} // namespace stellar::native_route
