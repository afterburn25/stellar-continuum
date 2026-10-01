#pragma once
#include <stellar/engine/localization.hpp>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace stellar::native_settlement {
// Core settlement outcomes, candidate reasons, reach assessments and plan
// statuses are stable English literals or fixed skeletons with dynamic
// fragments. Recompose them through the locale table at the display boundary
// so the preview dialog, notices and opportunity lists stay localized without
// changing the authoritative messages or saves.
[[nodiscard]] inline std::string localized_message(
    const stellar::engine::LocalizationTable *locale, std::string_view message);

[[nodiscard]] inline std::string localized_metric(
    const stellar::engine::LocalizationTable *locale, std::string_view metric) {
  if (!locale) return std::string(metric);
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return locale->contains(key) ? std::string(locale->translate(key))
                                 : std::string(fallback);
  };
  if (metric == "Distance unconfirmed")
    return tr("SETTLE_METRIC_DISTANCE_UNKNOWN", metric);
  if (metric == "Speed unconfirmed")
    return tr("SETTLE_METRIC_SPEED_UNKNOWN", metric);
  const auto divide = [](std::string_view text, std::string_view infix)
      -> std::optional<std::pair<std::string_view, std::string_view>> {
    const auto at = text.find(infix);
    if (at == std::string_view::npos) return std::nullopt;
    return std::pair{text.substr(0, at), text.substr(at + infix.size())};
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
  // "{sci} km \u00b7 {n} ly" / "{sci} km/day \u00b7 {n} ly/day".
  if (const auto p = divide(metric, " km \xc2\xb7 "))
    if (const auto ly = p->second; ly.ends_with(" ly"))
      return trf("SETTLE_METRIC_LY",
                 {std::string(p->first),
                  std::string(ly.substr(0, ly.size() - 3))},
                 "{0} km \u00b7 {1} ly");
  if (const auto p = divide(metric, " km/day \xc2\xb7 "))
    if (const auto ly = p->second; ly.ends_with(" ly/day"))
      return trf("SETTLE_METRIC_SPEED",
                 {std::string(p->first),
                  std::string(ly.substr(0, ly.size() - 6))},
                 "{0} km/day \u00b7 {1} ly/day");
  return std::string(metric);
}

[[nodiscard]] inline std::string localized_reach_reason(
    const stellar::engine::LocalizationTable *locale, std::string_view reason) {
  if (!locale || reason.empty()) return std::string(reason);
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
  static const std::pair<std::string_view, std::string_view> statics[] = {
      {"Mission is beyond current operational reach.", "SETTLE_REACH_BEYOND"},
      {"That fleet is not controlled by this civilization.",
       "SETTLE_REACH_NOT_CONTROLLED"},
      {"Unknown mission target.", "SETTLE_REACH_UNKNOWN_TARGET"},
      {"The fleet must finish its current lane leg before receiving a new "
       "interstellar route.",
       "SETTLE_REACH_IN_TRANSIT"},
      {"The outward route is reachable, but would leave insufficient fuel to "
       "reach any owned refuelling settlement.",
       "SETTLE_REACH_NO_RETURN"},
      {"The fleet is already in the target system.",
       "SETTLE_REACH_ALREADY_THERE"},
  };
  for (const auto &[literal, key] : statics)
    if (reason == literal) return tr(key, literal);
  // "No connected lane route is available within this fleet's {metric}
  //  maximum leg range."
  if (const auto rest = strip_prefix(
          reason,
          "No connected lane route is available within this fleet's "))
    if (const auto metric = strip_suffix(*rest, " maximum leg range."))
      return trf("SETTLE_REACH_NO_ROUTE",
                 {localized_metric(locale, *metric)},
                 "No connected lane route is available within this fleet's "
                 "{0} maximum leg range.");
  // "Insufficient fuel endurance for the lane into {system}: {metric}
  //  required, {metric} available before refueling."
  if (const auto rest = strip_prefix(
          reason, "Insufficient fuel endurance for the lane into "))
    if (const auto p = divide(*rest, ": "))
      if (const auto q = divide(p->second, " required, "))
        if (const auto available = strip_suffix(
                q->second, " available before refueling."))
          return trf("SETTLE_REACH_FUEL",
                     {std::string(p->first),
                      localized_metric(locale, q->first),
                      localized_metric(locale, *available)},
                     "Insufficient fuel endurance for the lane into {0}: {1} "
                     "required, {2} available before refueling.");
  // "Route: {n} lane leg(s), {metric} total; maximum leg {metric};
  //  projected fuel reserve {metric}."
  if (const auto rest = strip_prefix(reason, "Route: "))
    if (const auto p = divide(*rest, " lane leg")) {
      const auto tail = p->second.starts_with("s, ")
                            ? p->second.substr(3)
                  : p->second.starts_with(", ") ? p->second.substr(2)
                                                : std::string_view{};
      if (!tail.empty())
        if (const auto q = divide(tail, " total; maximum leg "))
          if (const auto r = divide(q->second, "; projected fuel reserve "))
            if (const auto fuel = strip_suffix(r->second, "."))
              return trf(p->first == "1" ? "SETTLE_REACH_ROUTE_ONE"
                                         : "SETTLE_REACH_ROUTE",
                         {std::string(p->first),
                          localized_metric(locale, q->first),
                          localized_metric(locale, r->first),
                          localized_metric(locale, *fuel)},
                         "Route: {0} lane leg(s), {1} total; maximum leg {2}; "
                         "projected fuel reserve {3}.");
    }
  return std::string(reason);
}

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
  // Issue-path suffixes append to the approval message; split them first and
  // localize the head recursively.
  if (const auto head = strip_suffix(
          message,
          " Destination updated; the original expedition authorization "
          "remains in effect."))
    return localized_message(locale, *head) + " " +
           tr("SETTLE_MSG_DEST_UPDATED",
              "Destination updated; the original expedition authorization "
              "remains in effect.");
  if (const auto p = divide(message, " Expedition funded for "))
    if (const auto cost = strip_suffix(p->second, "."))
      return localized_message(locale, p->first) + " " +
             trf("SETTLE_MSG_FUNDED", {std::string(*cost)},
                 "Expedition funded for {0}.");
  // "{fleet}: colony mission approved for {body} in {system} with {n} million
  //  {species} colonists aboard. {reason}"
  if (const auto p = divide(message, ": colony mission approved for "))
    if (const auto q = divide(p->second, " in "))
      if (const auto r = divide(q->second, " with "))
        if (const auto s = divide(r->second, " million "))
          if (const auto t = divide(s->second, " colonists aboard. "))
            return trf("SETTLE_MSG_COLONY_APPROVED",
                       {std::string(p->first), std::string(q->first),
                        std::string(r->first), std::string(s->first),
                        std::string(t->first),
                        localized_message(locale, t->second)},
                       "{0}: colony mission approved for {1} in {2} with {3} "
                       "million {4} colonists aboard. {5}");
  // "{fleet}: staffed resource outpost approved for {body} in {system}.
  //  {reason}"
  if (const auto p = divide(message,
                            ": staffed resource outpost approved for "))
    if (const auto q = divide(p->second, " in "))
      if (const auto r = divide(q->second, ". "))
        return trf("SETTLE_MSG_OUTPOST_APPROVED",
                   {std::string(p->first), std::string(q->first),
                    std::string(r->first),
                    localized_message(locale, r->second)},
                   "{0}: staffed resource outpost approved for {1} in {2}. {3}");
  // Candidate reasons — the same skeletons stand alone as denial text and
  // ride inside the approval tail.
  if (const auto name = strip_suffix(
          message, " has no solid settlement surface in the current colony "
                   "model."))
    return trf("SETTLE_REASON_NO_SURFACE", {std::string(*name)},
               "{0} has no solid settlement surface in the current colony "
               "model.");
  if (const auto name = strip_suffix(
          message, " has no solid surface for the current outpost model."))
    return trf("SETTLE_REASON_NO_SURFACE_OUTPOST", {std::string(*name)},
               "{0} has no solid surface for the current outpost model.");
  if (const auto rest = strip_prefix(
          message, "A native pre-warp civilization already inhabits "))
    if (const auto name = strip_suffix(*rest, "."))
      return trf("SETTLE_REASON_NATIVE", {std::string(*name)},
                 "A native pre-warp civilization already inhabits {0}.");
  if (const auto name = strip_suffix(
          message, " has no confirmed rare-resource deposit."))
    return trf("SETTLE_REASON_NO_DEPOSIT", {std::string(*name)},
               "{0} has no confirmed rare-resource deposit.");
  if (const auto p = divide(message, " is not currently viable for "))
    if (const auto q = divide(p->second,
                              ": natural habitability "))
      if (const auto r = divide(q->second,
                                ", unprotected operational capacity "))
        if (const auto tail = strip_suffix(
                r->second,
                ". Required habitat-support capabilities are not yet "
                "connected to colony construction/logistics."))
          return trf("SETTLE_REASON_NOT_VIABLE",
                     {std::string(p->first), std::string(q->first),
                      std::string(r->first), std::string(*tail)},
                     "{0} is not currently viable for {1}: natural "
                     "habitability {2}, unprotected operational capacity {3}. "
                     "Required habitat-support capabilities are not yet "
                     "connected to colony construction/logistics.");
  if (const auto rest = strip_prefix(
          message, "Another friendly colony ship (fleet "))
    if (const auto id = strip_suffix(
            *rest, ") is already committed to that system."))
      return trf("SETTLE_REASON_RESERVED", {std::string(*id)},
                 "Another friendly colony ship (fleet {0}) is already "
                 "committed to that system.");
  if (const auto amount = strip_suffix(
          message, " is required to fund the colony expedition."))
    return trf("SETTLE_REASON_COLONY_COST", {std::string(*amount)},
               "{0} is required to fund the colony expedition.");
  if (const auto amount = strip_suffix(
          message, " is required to fund the resource-outpost expedition."))
    return trf("SETTLE_REASON_OUTPOST_COST", {std::string(*amount)},
               "{0} is required to fund the resource-outpost expedition.");
  if (const auto p = divide(message, " is naturally viable for "))
    if (const auto q = divide(p->second, ". "))
      return trf("SETTLE_REASON_NATURAL",
                 {std::string(p->first), std::string(q->first),
                  localized_reach_reason(locale, q->second)},
                 "{0} is naturally viable for {1}. {2}");
  if (const auto p = divide(
          message,
          " is currently available through the prototype habitat-supported "
          "fallback for "))
    if (const auto q = divide(p->second, ". "))
      return trf("SETTLE_REASON_FALLBACK",
                 {std::string(p->first), std::string(q->first),
                  localized_reach_reason(locale, q->second)},
                 "{0} is currently available through the prototype "
                 "habitat-supported fallback for {1}. {2}");
  if (const auto p = divide(message, " can support a colony for "))
    if (const auto species = strip_suffix(
            p->second,
            "; use a colony ship instead of consuming a sealed outpost "
            "vessel."))
      return trf("SETTLE_REASON_USE_COLONY_SHIP",
                 {std::string(p->first), std::string(*species)},
                 "{0} can support a colony for {1}; use a colony ship "
                 "instead of consuming a sealed outpost vessel.");
  if (const auto p = divide(
          message,
          " is too harsh for colonization but its confirmed deposit can "
          "support a sealed staffed outpost. "))
    return trf("SETTLE_REASON_HARSH",
               {std::string(p->first),
                localized_reach_reason(locale, p->second)},
               "{0} is too harsh for colonization but its confirmed deposit "
               "can support a sealed staffed outpost. {1}");
  // Plan statuses with count-driven singular/plural forms.
  if (const auto p = divide(
          message, " fully surveyed settlement candidate"))
    if (const auto tail = strip_suffix(
            p->second, " evaluated; none are currently orderable."))
      if (tail->empty() || *tail == "s")
        return trf(p->first == "1" ? "SETTLE_STATUS_CANDIDATES_ONE"
                                   : "SETTLE_STATUS_CANDIDATES",
                   {std::string(p->first)},
                   "{0} fully surveyed settlement candidate(s) evaluated; "
                   "none are currently orderable.");
  if (const auto p = divide(
          message, " currently orderable settlement opportunit"))
    if (const auto q = divide(p->second, " in a "))
      if (const auto window = strip_suffix(q->second,
                                         "-body planning window."))
        if ((q->first == "y" || q->first == "ies") && !window->empty())
          return trf(p->first == "1" ? "SETTLE_STATUS_OPPORTUNITIES_ONE"
                                     : "SETTLE_STATUS_OPPORTUNITIES",
                     {std::string(p->first), std::string(*window)},
                     "{0} currently orderable settlement opportunit(y/ies) "
                     "in a {1}-body planning window.");
  if (const auto p = divide(message, " valuable world"))
    if (const auto tail = strip_suffix(
            p->second,
            " evaluated; none can currently receive a staffed outpost."))
      if (tail->empty() || *tail == "s")
        return trf(p->first == "1" ? "SETTLE_STATUS_WORLDS_ONE"
                                   : "SETTLE_STATUS_WORLDS",
                   {std::string(p->first)},
                   "{0} valuable world(s) evaluated; none can currently "
                   "receive a staffed outpost.");
  if (const auto p = divide(
          message, " staffed resource-outpost opportunit"))
    if (const auto q = divide(p->second, " available in a "))
      if (const auto window = strip_suffix(q->second,
                                         "-world planning window."))
        if ((q->first == "y" || q->first == "ies") && !window->empty())
          return trf(p->first == "1" ? "SETTLE_STATUS_OUTPOST_ONE"
                                     : "SETTLE_STATUS_OUTPOST",
                     {std::string(p->first), std::string(*window)},
                     "{0} staffed resource-outpost opportunit(y/ies) "
                     "available in a {1}-world planning window.");
  // Reach reasons also stand alone when reach assessment fails before the
  // candidate composer runs.
  if (const auto localized = localized_reach_reason(locale, message);
      localized != message)
    return localized;
  static const std::pair<std::string_view, std::string_view> statics[] = {
      {"Unknown destination.", "SETTLE_MSG_UNKNOWN_DEST"},
      {"That astronomical target has not been detected yet.",
       "SETTLE_MSG_TARGET_UNDETECTED"},
      {"A completed science survey is required before a colony mission can "
       "be prepared.",
       "SETTLE_MSG_SURVEY_REQUIRED"},
      {"That planetary body is not part of the surveyed destination system.",
       "SETTLE_MSG_BODY_NOT_IN_SYSTEM"},
      {"Detailed planetary suitability is not legitimately known for that "
       "body.",
       "SETTLE_MSG_SUITABILITY_UNKNOWN"},
      {"No active colony ship carrying reserved colonists with that fleet "
       "ID is available.",
       "SETTLE_MSG_NO_COLONY_SHIP"},
      {"No colony ship carrying reserved colonists is available.",
       "SETTLE_MSG_NO_COLONY_SHIP_ANY"},
      {"No populated colony ship is available.",
       "SETTLE_MSG_NO_POPULATED_SHIP"},
      {"The colony ship's passenger species identity is invalid.",
       "SETTLE_MSG_SPECIES_INVALID"},
      {"No active staffed resource-outpost vessel with that fleet ID is "
       "available.",
       "SETTLE_MSG_NO_OUTPOST_VESSEL"},
      {"The outpost vessel's specialist personnel species identity is "
       "invalid.",
       "SETTLE_MSG_OUTPOST_SPECIES_INVALID"},
      {"That body is not a fully surveyed rare-resource outpost candidate.",
       "SETTLE_MSG_NOT_OUTPOST_CANDIDATE"},
      {"No surveyed body in that system is currently viable for the colony "
       "ship's population. Additional environmental-support capability may "
       "make other worlds usable later.",
       "SETTLE_MSG_NO_VIABLE_BODY"},
      {"Extreme stellar irradiation prohibits approach, habitation and "
       "surface operations.",
       "SETTLE_REASON_BAKED"},
      {"Extreme stellar irradiation prohibits close approach and resource "
       "extraction.",
       "SETTLE_REASON_BAKED_OUTPOST"},
      {"That system already contains a founded colony in the current "
       "single-colony early-release model.",
       "SETTLE_REASON_OCCUPIED"},
      {"That system already contains a settlement in the current "
       "single-settlement model.",
       "SETTLE_REASON_OCCUPIED_OUTPOST"},
      {"Another friendly settlement vessel is already committed to that "
       "system.",
       "SETTLE_REASON_OUTPOST_RESERVED"},
      {"No fully surveyed planetary bodies are currently available for "
       "settlement evaluation.",
       "SETTLE_STATUS_NO_CANDIDATES"},
      {"No fully surveyed rare-resource worlds are currently available for "
       "outpost evaluation.",
       "SETTLE_STATUS_NO_OUTPOST_CANDIDATES"},
  };
  for (const auto &[literal, key] : statics)
    if (message == literal) return tr(key, literal);
  return std::string(message);
}
} // namespace stellar::native_settlement
