#pragma once
#include <stellar/engine/localization.hpp>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace stellar::native_research {
// Adaptive-research eligibility blockers and command outcomes are stable
// English literals or fixed skeletons with dynamic fragments (node names and
// formatted currency). Recompose them through the locale table at the display
// boundary so workspace notices, node-card blockers, statuses and
// notifications stay localized without changing authoritative results or
// saves.
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
  static const std::pair<std::string_view, std::string_view> statics[] = {
      {"A required specialist research-facility capability is unavailable.",
       "RESEARCH_BLOCK_FACILITY"},
      {"At least one acceptable specialist research-facility capability is "
       "required.",
       "RESEARCH_BLOCK_FACILITY_ANY"},
      {"Unknown research possibility.", "RESEARCH_BLOCK_UNKNOWN"},
      {"This possibility is not part of normal public research.",
       "RESEARCH_BLOCK_NOT_PUBLIC"},
      {"Additional prerequisite knowledge is required.",
       "RESEARCH_BLOCK_PREREQ"},
      {"At least one alternative prerequisite knowledge path is required.",
       "RESEARCH_BLOCK_PREREQ_ANY"},
      {"The target research context is not compatible with this possibility.",
       "RESEARCH_BLOCK_CONTEXT"},
      {"Required scientific evidence is not currently available.",
       "RESEARCH_BLOCK_EVIDENCE"},
      {"The recognized research need/evidence pressure is below the required "
       "level.",
       "RESEARCH_BLOCK_PRESSURE"},
      {"None of the recognized conditions that would justify this program are "
       "strong enough yet.",
       "RESEARCH_BLOCK_PRESSURE_ANY"},
      {"A required functional capability is missing.",
       "RESEARCH_BLOCK_CAPABILITY"},
      {"At least one alternative functional capability is required.",
       "RESEARCH_BLOCK_CAPABILITY_ANY"},
      {"This knowledge is already mature/established.",
       "RESEARCH_BLOCK_MATURE"},
      {"The possibility is recognized but not yet Investigable.",
       "RESEARCH_BLOCK_NOT_INVESTIGABLE"},
      {"The possibility has not entered the visible research horizon.",
       "RESEARCH_BLOCK_HORIZON"},
      {"This research project is already active or paused.",
       "RESEARCH_BLOCK_ACTIVE"},
      {"The project needs more assigned Effective Research Labs before it can "
       "begin.",
       "RESEARCH_BLOCK_LABS_MIN"},
      {"The project needs more assigned Effective Research Labs.",
       "RESEARCH_BLOCK_LABS_MORE"},
      {"Restart this research in its original applicability context.",
       "RESEARCH_BLOCK_CONTEXT_RESTART"},
      {"Not enough unassigned Effective Research Labs are available.",
       "RESEARCH_BLOCK_LABS_UNASSIGNED"},
      {"Not enough free Effective Research Labs are available.",
       "RESEARCH_BLOCK_LABS_FREE"},
      {"No directed research-program capacity is free.",
       "RESEARCH_BLOCK_CAPACITY"},
      {"Research project cannot start under current conditions.",
       "RESEARCH_MSG_START_BLOCKED"},
      {"No active research project exists for that node.",
       "RESEARCH_MSG_NO_ACTIVE"},
      {"Research project is already paused.", "RESEARCH_MSG_ALREADY_PAUSED"},
      {"Research project paused; accumulated scientific work is preserved.",
       "RESEARCH_MSG_PAUSED"},
      {"The project is not currently paused.", "RESEARCH_MSG_NOT_PAUSED"},
      {"This hypothesis requires scientific resolution before research can "
       "resume.",
       "RESEARCH_MSG_HYPOTHESIS_GATE"},
      {"Research project cannot resume under current conditions.",
       "RESEARCH_MSG_RESUME_BLOCKED"},
      {"Research project resumed.", "RESEARCH_MSG_RESUMED"},
      {"No research project exists for that node.", "RESEARCH_MSG_NO_PROJECT"},
      {"Allocation is below the project's minimum lab requirement.",
       "RESEARCH_MSG_LABS_MINIMUM"},
      {"Not enough Effective Research Labs are available for that "
       "allocation.",
       "RESEARCH_MSG_LABS_ALLOCATION"},
      {"No hypothesis is awaiting scientific resolution for that node.",
       "RESEARCH_MSG_NO_HYPOTHESIS"},
      {"That node is not a scientific hypothesis.",
       "RESEARCH_MSG_NOT_HYPOTHESIS"},
      {"Unknown scenario technology.", "RESEARCH_MSG_SCENARIO_UNKNOWN"},
      {"Scenario research requires an applicability context.",
       "RESEARCH_MSG_SCENARIO_CONTEXT"},
      {"Technology is already mature.", "RESEARCH_MSG_ALREADY_MATURE"},
      {"Scenario technology established.", "RESEARCH_MSG_SCENARIO_DONE"},
      {"No valid treasury is available to receive the research refund.",
       "RESEARCH_MSG_REFUND_TREASURY"},
      {"Research civilization is unavailable.", "RESEARCH_MSG_NO_CIVILIZATION"},
      {"That technology has not been discovered.",
       "RESEARCH_MSG_UNDISCOVERED"},
      {"This program is already active or concluded.",
       "RESEARCH_MSG_PROGRAM_ENDED"},
      {"Research plan is full (128 entries).", "RESEARCH_MSG_PLAN_FULL"},
      {"The queued program cannot move in that direction.",
       "RESEARCH_MSG_PLAN_MOVE"},
      {"Unknown research plan command.", "RESEARCH_MSG_PLAN_COMMAND"},
      {"Research plan updated. Queued programs start in order when time is "
       "running and their requirements are met.",
       "RESEARCH_MSG_PLAN_UPDATED"},
  };
  for (const auto &[literal, key] : statics)
    if (message == literal) return tr(key, literal);
  // "{node} already has an active milestone funding commitment."
  if (const auto name = strip_suffix(
          message, " already has an active milestone funding commitment."))
    return trf("RESEARCH_MSG_FUNDED", {std::string(*name)},
               "{0} already has an active milestone funding commitment.");
  // "{node} requires {auth} to authorize and {milestones} for prototype
  //  milestones, plus {operating} for its first operating day; {available}
  //  is available."
  if (const auto p = divide(message, " requires "))
    if (const auto q = divide(p->second, " to authorize and "))
      if (const auto r = divide(q->second, " for prototype milestones, plus "))
        if (const auto s =
                divide(r->second, " for its first operating day; "))
          if (const auto available = strip_suffix(s->second, " is available."))
            return trf("RESEARCH_MSG_FUNDING_REQUIRED",
                       {std::string(p->first), std::string(q->first),
                        std::string(r->first), std::string(s->first),
                        std::string(*available)},
                       "{0} requires {1} to authorize and {2} for prototype "
                       "milestones, plus {3} for its first operating day; {4} "
                       "is available.");
  // "Directed research started: {node}." plus the optional funding tail
  // " Authorized for {auth}; {milestones} reserved for prototypes and
  //  validation; planned operations cost {rate}."
  if (const auto rest = strip_prefix(message, "Directed research started: ")) {
    if (const auto p = divide(*rest, ". Authorized for "))
      if (const auto q = divide(
              p->second,
              " reserved for prototypes and validation; planned operations "
              "cost "))
        if (const auto rate = strip_suffix(q->second, "."))
          if (const auto amounts = divide(q->first, "; "))
            return trf("RESEARCH_MSG_STARTED_FUNDING",
                       {std::string(p->first), std::string(amounts->first),
                        std::string(amounts->second), std::string(*rate)},
                     "Directed research started: {0}. Authorized for {1}; {2} "
                     "reserved for prototypes and validation; planned "
                     "operations cost {3}.");
    if (const auto name = strip_suffix(*rest, "."))
      return trf("RESEARCH_MSG_STARTED", {std::string(*name)},
                 "Directed research started: {0}.");
  }
  // "Research cancelled; laboratories released and scientific work
  //  preserved." plus the optional refund tail.
  if (const auto rest =
          strip_prefix(message, "Research cancelled; laboratories released "
                                "and scientific work preserved.")) {
    if (rest->empty())
      return tr("RESEARCH_MSG_CANCELLED",
                "Research cancelled; laboratories released and scientific "
                "work preserved.");
    if (const auto refund = strip_suffix(
            *rest,
            " in unused milestone funds. Authorization and operating costs "
            "are not refundable. Restarting requires new authorization and "
            "milestone funding."))
      if (const auto amount = strip_prefix(*refund, " Refunded "))
        return trf("RESEARCH_MSG_CANCELLED_REFUND", {std::string(*amount)},
                   "Research cancelled; laboratories released and scientific "
                   "work preserved. Refunded {0} in unused milestone funds. "
                   "Authorization and operating costs are not refundable. "
                   "Restarting requires new authorization and milestone "
                   "funding.");
  }
  // "Research lab allocation changed to {n} effective labs."
  if (const auto count = strip_prefix(
          message, "Research lab allocation changed to "))
    if (const auto labs = strip_suffix(*count, " effective labs."))
      return trf("RESEARCH_MSG_LABS_CHANGED", {std::string(*labs)},
                 "Research lab allocation changed to {0} effective labs.");
  // "{node} was disproven; accumulated negative knowledge is preserved."
  if (const auto name = strip_suffix(
          message,
          " was disproven; accumulated negative knowledge is preserved."))
    return trf("RESEARCH_MSG_DISPROVEN", {std::string(*name)},
               "{0} was disproven; accumulated negative knowledge is "
               "preserved.");
  // "Evidence supports {node}; the principle is Demonstrated."
  if (const auto rest = strip_prefix(message, "Evidence supports "))
    if (const auto name =
            strip_suffix(*rest, "; the principle is Demonstrated."))
      return trf("RESEARCH_MSG_DEMONSTRATED", {std::string(*name)},
                 "Evidence supports {0}; the principle is Demonstrated.");
  return std::string(message);
}
} // namespace stellar::native_research
