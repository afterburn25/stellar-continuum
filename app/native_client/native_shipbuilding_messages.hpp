#pragma once
#include "native_data_names.hpp"
#include <stellar/engine/localization.hpp>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace stellar::native_shipbuilding {
// Core shipbuilding outcomes and blockers are stable English literals or fixed
// skeletons with dynamic fragments. Recompose them through the locale table at
// the display boundary so the notification feed, fact rows and hover reasons
// stay localized without changing the authoritative messages or saves.
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
  // Authored names embedded in composed skeletons (design, project,
  // capability and upgrade-requirement names) resolve through their stable
  // catalog keys so localized blockers don't embed English fragments.
  const auto localize_fragment = [&](std::string_view text) {
    std::string out =
        stellar::native_data::localized_authored_fragment(locale, text);
    if (out.starts_with("one of "))
      out = tr("SHIPYARD_REQ_ONE_OF", "one of ") + out.substr(7);
    for (std::size_t at = out.find(" or "); at != std::string::npos;
         at = out.find(" or "))
      out.replace(at, 4, tr("SHIPYARD_REQ_OR", " or "));
    return out;
  };
  // "{design} requires {list}." — locked designs embed a requirement list.
  if (const auto p = divide(message, " requires "))
    if (const auto list = strip_suffix(p->second, "."))
      return trf("SHIPYARD_DESIGN_REQUIRES",
                 {localize_fragment(p->first), localize_fragment(*list)},
                 "{0} requires {1}.");
  if (const auto list = strip_prefix(message, "requires "))
    return trf("SHIPYARD_REQUIRES", {localize_fragment(*list)},
               "requires {0}");
  // "{cost} is required to authorize {design}."
  if (const auto p = divide(message, " is required to authorize "))
    if (const auto name = strip_suffix(p->second, "."))
      return trf("SHIPYARD_AUTH_REQUIRED",
                 {std::string(p->first), localize_fragment(*name)},
                 "{0} is required to authorize {1}.");
  // "At least {n} million population is required before reserving colonists
  //  for this ship."
  if (const auto rest = strip_prefix(message, "At least "))
    if (const auto amount = strip_suffix(
            *rest,
            " million population is required before reserving colonists for "
            "this ship."))
      return trf("SHIPYARD_POPULATION_REQUIRED", {std::string(*amount)},
                 "At least {0} million population is required before "
                 "reserving colonists for this ship.");
  // "The source colony references unknown population species '{id}'."
  if (const auto rest = strip_prefix(
          message, "The source colony references unknown population species '"))
    if (const auto id = strip_suffix(*rest, "'."))
      return trf("SHIPYARD_UNKNOWN_SPECIES", {std::string(*id)},
                 "The source colony references unknown population species "
                 "'{0}'.");
  // Start/queue outcomes.
  if (const auto rest = strip_prefix(message, "Ship construction started: "))
    if (const auto q = divide(*rest, ". Authorized for "))
      if (const auto cost = strip_suffix(q->second, "."))
        return trf("SHIPYARD_MSG_STARTED",
                   {localize_fragment(q->first), std::string(*cost)},
                   "Ship construction started: {0}. Authorized for {1}.");
  if (const auto rest = strip_prefix(message, "Queued "))
    if (const auto q = divide(*rest, " for "))
      if (const auto t = divide(q->second, ". "))
        if (const auto n = strip_suffix(
                t->second, "/8 pending vessel slots are now in use."))
          return trf("SHIPYARD_MSG_QUEUED",
                     {localize_fragment(q->first), std::string(t->first),
                      std::string(*n)},
                     "Queued {0} for {1}. {2}/8 pending vessel slots are now "
                     "in use.");
  // Batch outcomes; the failure embeds a per-order message.
  if (const auto inner = strip_prefix(message, "Batch not ordered: "))
    return trf("SHIPYARD_BATCH_FAILED",
               {localized_message(locale, *inner)},
               "Batch not ordered: {0}");
  if (const auto n = strip_suffix(
          message,
          " vessels authorized. Industry is consumed as construction "
          "progresses."))
    return trf("SHIPYARD_BATCH_AUTHORIZED", {std::string(*n)},
               "{0} vessels authorized. Industry is consumed as construction "
               "progresses.");
  // Stable denial/notice literals from the core assessment paths.
  static const std::pair<std::string_view, std::string_view> statics[] = {
      {"Unknown civilization.", "SHIPYARD_DENY_CIV"},
      {"The shipyard queue is full (8 pending vessels maximum).",
       "SHIPYARD_DENY_QUEUE_FULL"},
      {"Unknown ship design.", "SHIPYARD_DENY_DESIGN"},
      {"This shipyard has invalid or duplicate vessel order identities.",
       "SHIPYARD_DENY_ORDER_IDS"},
      {"This shipyard's order counter does not follow its existing vessel "
       "identities.",
       "SHIPYARD_DENY_ORDER_COUNTER"},
      {"This shipyard's next order identity collides with an existing vessel "
       "order.",
       "SHIPYARD_DENY_ORDER_COLLISION"},
      {"This shipyard cannot allocate another stable order identity.",
       "SHIPYARD_DENY_ORDER_EXHAUSTED"},
      {"The next queued vessel references an unknown design and cannot be "
       "promoted.",
       "SHIPYARD_DENY_PROMOTE_DESIGN"},
      {"The next queued vessel has invalid accounting and cannot be "
       "promoted.",
       "SHIPYARD_DENY_PROMOTE_ACCOUNTING"},
      {"Reserved colonists cannot be returned because their original colony "
       "is no longer a valid owned source.",
       "SHIPYARD_DENY_COLONIST_SOURCE"},
      {"Reserved colonists cannot be returned because their original colony "
       "has invalid population accounting.",
       "SHIPYARD_DENY_COLONIST_ACCOUNTING"},
      {"Choose a quantity within the shipyard queue capacity.",
       "SHIPYARD_DENY_QUANTITY"},
      {"Invalid queue direction.", "SHIPYARD_DENY_DIRECTION"},
      {"Shipyard is unavailable.", "SHIPYARD_DENY_UNAVAILABLE"},
      {"Only waiting orders can be reordered. The active vessel keeps its "
       "progress.",
       "SHIPYARD_DENY_REORDER_ACTIVE"},
      {"That order is already at the end of the waiting queue.",
       "SHIPYARD_DENY_REORDER_END"},
      {"Waiting build order updated.", "SHIPYARD_MSG_REORDERED"},
      {"Unknown shipyard order.", "SHIPYARD_DENY_ORDER_UNKNOWN"},
      {"That shipyard order is no longer pending.", "SHIPYARD_MSG_ORDER_GONE"},
      {"Shipyard order identity is ambiguous; repair the save before "
       "cancelling.",
       "SHIPYARD_DENY_ORDER_AMBIGUOUS"},
      {"The shipyard order references an unknown design.",
       "SHIPYARD_DENY_ORDER_DESIGN"},
      {"The shipyard order has invalid accounting data.",
       "SHIPYARD_DENY_ORDER_ACCOUNTING"},
      {"The shipyard order records more progress than the design requires.",
       "SHIPYARD_DENY_ORDER_PROGRESS"},
      {"The refund cannot be represented in the civilization treasury.",
       "SHIPYARD_DENY_REFUND"},
  };
  for (const auto &[literal, key] : statics)
    if (message == literal) return tr(key, literal);
  return std::string(message);
}
} // namespace stellar::native_shipbuilding
