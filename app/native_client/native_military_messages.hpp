#pragma once
#include <stellar/engine/localization.hpp>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace stellar::native_military {
// Core military-order outcomes are stable English literals or fixed
// skeletons with dynamic fragments (fleet names, system ids, target
// names). Recompose them through the locale table at the display
// boundary so the fleet-command notice stays localized without changing
// the authoritative order results or saves.
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
  const auto divide = [](std::string_view text, std::string_view infix)
      -> std::optional<std::pair<std::string_view, std::string_view>> {
    const auto at = text.find(infix);
    if (at == std::string_view::npos) return std::nullopt;
    return std::pair{text.substr(0, at), text.substr(at + infix.size())};
  };
  // Tactical order acknowledgments embed the engine's PascalCase order name
  // ("{formation} acknowledged StandoffAttack."). Map it back to the battle
  // workspace's localized order vocabulary.
  const auto tactical_order_name =
      [&](std::string_view core_name) -> std::optional<std::string> {
    static const std::pair<std::string_view, std::string_view> names[] = {
        {"Engage", "BATTLE_ORDER_ENGAGE"},
        {"Hold", "BATTLE_ORDER_HOLD"},
        {"Defend", "BATTLE_ORDER_DEFEND"},
        {"AdvanceCautiously", "BATTLE_ORDER_ADVANCE_CAUTIOUS"},
        {"Advance", "BATTLE_ORDER_ADVANCE"},
        {"StandoffAttack", "BATTLE_ORDER_STANDOFF"},
        {"Screen", "BATTLE_ORDER_SCREEN"},
        {"ProtectCriticalAsset", "BATTLE_ORDER_PROTECT"},
        {"FocusFire", "BATTLE_ORDER_FOCUS"},
        {"FlankLeft", "BATTLE_ORDER_FLANK_LEFT"},
        {"FlankRight", "BATTLE_ORDER_FLANK_RIGHT"},
        {"Intercept", "BATTLE_ORDER_INTERCEPT"},
        {"Pursue", "BATTLE_ORDER_PURSUE"},
        {"BreakContact", "BATTLE_ORDER_BREAK"},
        {"Disengage", "BATTLE_ORDER_DISENGAGE"},
        {"Retreat", "BATTLE_ORDER_RETREAT"},
        {"EmergencyRetreat", "BATTLE_ORDER_EMERGENCY"},
        {"Breakout", "BATTLE_ORDER_BREAKOUT"},
        {"Surrender", "BATTLE_ORDER_SURRENDER"},
    };
    for (const auto &[literal, key] : names)
      if (core_name == literal) return tr(key, literal);
    return std::nullopt;
  };
  // Accepted-order results.
  if (const auto name = strip_suffix(message, " is holding position."))
    return trf("MIL_ORDER_HOLDING", {std::string(*name)},
               "{0} is holding position.");
  if (const auto p = divide(message, " is defending system "))
    if (const auto id = strip_suffix(p->second, "."))
      return trf("MIL_ORDER_DEFENDING",
                 {std::string(p->first), std::string(*id)},
                 "{0} is defending system {1}.");
  if (const auto p = divide(message, " is attacking "))
    if (const auto target = strip_suffix(p->second, "."))
      return trf("MIL_ORDER_ATTACKING",
                 {std::string(p->first), std::string(*target)},
                 "{0} is attacking {1}.");
  if (const auto name = strip_suffix(message, " is attempting to disengage."))
    return trf("MIL_ORDER_DISENGAGING", {std::string(*name)},
               "{0} is attempting to disengage.");
  // Preview capability strings.
  if (const auto name = strip_suffix(message, " can hold position."))
    return trf("MIL_ORDER_CAN_HOLD", {std::string(*name)},
               "{0} can hold position.");
  if (const auto p = divide(message, " can defend system "))
    if (const auto id = strip_suffix(p->second, "."))
      return trf("MIL_ORDER_CAN_DEFEND",
                 {std::string(p->first), std::string(*id)},
                 "{0} can defend system {1}.");
  if (const auto p = divide(message, " can attack "))
    if (const auto target = strip_suffix(p->second, "."))
      return trf("MIL_ORDER_CAN_ATTACK",
                 {std::string(p->first), std::string(*target)},
                 "{0} can attack {1}.");
  if (const auto name = strip_suffix(message, " can attempt to disengage."))
    return trf("MIL_ORDER_CAN_DISENGAGE", {std::string(*name)},
               "{0} can attempt to disengage.");
  if (const auto name = strip_suffix(
          message, " has no combat-capable weapon system."))
    return trf("MIL_ORDER_NO_WEAPON", {std::string(*name)},
               "{0} has no combat-capable weapon system.");
  static const std::pair<std::string_view, std::string_view> statics[] = {
      {"No active fleet with that identity belongs to the civilization.",
       "MIL_ORDER_NO_FLEET"},
      {"A defend order currently requires the fleet to be present in the "
       "defended system.",
       "MIL_ORDER_DEFEND_PRESENT"},
      {"An attack order requires a target fleet.", "MIL_ORDER_ATTACK_TARGET"},
      {"The requested target is not a valid hostile fleet.",
       "MIL_ORDER_INVALID_TARGET"},
      {"The target must be in the same system before combat can begin.",
       "MIL_ORDER_SAME_SYSTEM"},
      {"The target has tactically disengaged from this system-level "
       "engagement.",
       "MIL_ORDER_DISENGAGED"},
      {"Diplomatic/political state does not currently permit a hostile "
       "engagement.",
       "MIL_ORDER_NO_HOSTILITY"},
      {"Unknown military order.", "MIL_ORDER_UNKNOWN"},
      // Tactical engagement results (campaign massive-combat begin and
      // per-formation order issues).
      {"Combat time is invalid.", "MIL_TACTICAL_BAD_TIME"},
      {"An encounter is already active. Use its tactical orders.",
       "MIL_TACTICAL_ACTIVE"},
      {"An armed fleet must be stationed in a system before engaging.",
       "MIL_TACTICAL_STATIONED"},
      {"No attackable hostile formation is detected in this system.",
       "MIL_TACTICAL_NO_HOSTILE"},
      {"A participating vessel has no combat-ready hull.",
       "MIL_TACTICAL_NO_HULL"},
      {"The participating vessels require more damage-compatible tactical "
       "groups than the formation limit permits.",
       "MIL_TACTICAL_GROUPS"},
      {"There is no active tactical encounter.", "MIL_TACTICAL_NONE"},
      {"The selected fleet must be present in a star system to engage "
       "hostiles.",
       "MIL_TACTICAL_NO_SYSTEM"},
      {"No attackable hostile fleet is detected in this fleet's current "
       "system.",
       "MIL_TACTICAL_NO_TARGET"},
      {"The combat order contains an invalid tactical mode.",
       "MIL_TACTICAL_BAD_MODE"},
      {"Combat objective must be finite.", "MIL_TACTICAL_BAD_OBJECTIVE"},
      {"No active owned formation has that identity.",
       "MIL_TACTICAL_NO_FORMATION"},
      {"The protected asset must be an active friendly formation.",
       "MIL_TACTICAL_PROTECT_TARGET"},
      {"The requested target is not an active hostile formation.",
       "MIL_TACTICAL_TARGET_HOSTILE"},
      {"That combat order requires a target formation.",
       "MIL_TACTICAL_NEEDS_TARGET"},
  };
  for (const auto &[literal, key] : statics)
    if (message == literal) return tr(key, literal);
  // "This encounter exceeds the {n}-ship tactical limit."
  if (const auto rest = strip_prefix(message, "This encounter exceeds the "))
    if (const auto count = strip_suffix(*rest, "-ship tactical limit."))
      return trf("MIL_TACTICAL_LIMIT", {std::string(*count)},
                 "This encounter exceeds the {0}-ship tactical limit.");
  // "Encounter established: {n} commissioned vessels. Tactical orders
  //  ready."
  if (const auto rest = strip_prefix(message, "Encounter established: "))
    if (const auto count = strip_suffix(
            *rest, " commissioned vessels. Tactical orders ready."))
      return trf("MIL_TACTICAL_ESTABLISHED", {std::string(*count)},
                 "Encounter established: {0} commissioned vessels. Tactical "
                 "orders ready.");
  // "{formation} surrendered."
  if (const auto name = strip_suffix(message, " surrendered."))
    return trf("MIL_TACTICAL_SURRENDERED", {std::string(*name)},
               "{0} surrendered.");
  // "{formation} acknowledged surrender."
  if (const auto name = strip_suffix(message, " acknowledged surrender."))
    return trf("MIL_TACTICAL_ACK_SURRENDER", {std::string(*name)},
               "{0} acknowledged surrender.");
  // "{formation}: {OrderName}." and "{formation} acknowledged {OrderName}."
  if (const auto p = divide(message, ": "))
    if (const auto order = strip_suffix(p->second, "."))
      if (const auto name = tactical_order_name(*order))
        return trf("MIL_TACTICAL_ORDER_EVENT",
                   {std::string(p->first), *name},
                   "{0}: {1}.");
  if (const auto p = divide(message, " acknowledged "))
    if (const auto order = strip_suffix(p->second, "."))
      if (const auto name = tactical_order_name(*order))
        return trf("MIL_TACTICAL_ACK_ORDER",
                   {std::string(p->first), *name},
                   "{0} acknowledged {1}.");
  return std::string(message);
}
} // namespace stellar::native_military
