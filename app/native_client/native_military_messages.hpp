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
  };
  for (const auto &[literal, key] : statics)
    if (message == literal) return tr(key, literal);
  return std::string(message);
}
} // namespace stellar::native_military
