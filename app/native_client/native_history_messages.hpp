#pragma once
#include <stellar/engine/localization.hpp>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace stellar::native_history {
// EventHistory records store stable English summaries authored by the
// core event producers (diplomacy/war/exploration/colonization/
// research journals). Recompose them through the locale table at the
// chronicle's projection boundary so history entries display localized
// without changing the authoritative records or the save format.
[[nodiscard]] inline std::string localized_history_summary(
    const stellar::engine::LocalizationTable *locale, std::string_view category,
    std::string_view summary) {
  if (!locale || summary.empty()) return std::string(summary);
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

  if (category.starts_with("diplomacy.")) {
    // The diplomatic journal substitutes fixed kind phrases; map each.
    static const std::pair<std::string_view, std::string_view> statics[] = {
        {"A contact was observed.", "HISTORY_DIP_CONTACT_OBSERVED"},
        {"A diplomatic contact was established.", "HISTORY_DIP_CONTACT"},
        {"A communication channel is now available.", "HISTORY_DIP_CHANNEL"},
        {"A diplomatic contact was lost.", "HISTORY_DIP_CONTACT_LOST"},
        {"Territorial access changed.", "HISTORY_DIP_ACCESS"},
        {"A territorial claim was asserted.", "HISTORY_DIP_CLAIM_ASSERTED"},
        {"A territorial claim was communicated.", "HISTORY_DIP_CLAIM_SENT"},
        {"A territorial claim received a response.",
         "HISTORY_DIP_CLAIM_RESPONSE"},
        {"A border warning was issued.", "HISTORY_DIP_BORDER_WARNING"},
        {"A trespass was recorded.", "HISTORY_DIP_TRESPASS"},
        {"A diplomatic proposal has been sent.", "HISTORY_DIP_PROPOSAL_SENT"},
        {"A diplomatic proposal has been accepted.",
         "HISTORY_DIP_PROPOSAL_ACCEPTED"},
        {"A diplomatic proposal has been declined.",
         "HISTORY_DIP_PROPOSAL_DECLINED"},
        {"A diplomatic proposal was withdrawn.",
         "HISTORY_DIP_PROPOSAL_WITHDRAWN"},
        {"A diplomatic proposal has expired.", "HISTORY_DIP_PROPOSAL_EXPIRED"},
        {"A diplomatic agreement is now active.",
         "HISTORY_DIP_AGREEMENT_ACTIVE"},
        {"A diplomatic agreement has ended.", "HISTORY_DIP_AGREEMENT_ENDED"},
        {"A diplomatic relationship has changed.", "HISTORY_DIP_RELATIONSHIP"},
        {"A declaration of war has been recorded.", "HISTORY_DIP_WAR"},
        {"A diplomatic event has been recorded.", "HISTORY_DIP_EVENT"},
    };
    for (const auto &[literal, key] : statics)
      if (summary == literal) return tr(key, literal);
    return std::string(summary);
  }

  if (category.starts_with("war.")) {
    // "{actor} engaged {target}." — engagement start.
    if (const auto p = divide(summary, " engaged "))
      if (const auto target = strip_suffix(p->second, "."))
        return trf("HISTORY_WAR_ENGAGED",
                   {std::string(p->first), std::string(*target)},
                   "{0} engaged {1}.");
    // "{actor} hit {target}: {d} damage ({s} shields, {a} armor, {h} hull)."
    if (const auto p = divide(summary, " hit "))
      if (const auto q = divide(p->second, ": "))
        if (const auto r = divide(q->second, " damage ("))
          if (const auto s = divide(r->second, " shields, "))
            if (const auto t = divide(s->second, " armor, "))
              if (const auto hull = strip_suffix(t->second, " hull)."))
                return trf("HISTORY_WAR_HIT",
                           {std::string(p->first), std::string(q->first),
                            std::string(r->first), std::string(s->first),
                            std::string(t->first), std::string(*hull)},
                           "{0} hit {1}: {2} damage ({3} shields, {4} "
                           "armor, {5} hull).");
    // "{fleet} was destroyed by {attacker}." + optional casualty tail.
    if (const auto p = divide(summary, " was destroyed by ")) {
      if (const auto name = strip_suffix(p->second, "."))
        return trf("HISTORY_WAR_DESTROYED",
                   {std::string(p->first), std::string(*name)},
                   "{0} was destroyed by {1}.");
      if (const auto q = divide(p->second, ". "))
        if (const auto amount = strip_suffix(
                q->second, " million embarked population were lost."))
          return trf("HISTORY_WAR_DESTROYED_POP",
                     {std::string(p->first), std::string(q->first),
                      std::string(*amount)},
                     "{0} was destroyed by {1}. {2} million embarked "
                     "population were lost.");
    }
    // "{fleet} was lost in combat." — massive-combat reconciliation.
    if (const auto name = strip_suffix(summary, " was lost in combat."))
      return trf("HISTORY_WAR_LOST", {std::string(*name)},
                 "{0} was lost in combat.");
    if (const auto name =
            strip_suffix(summary, " began tactical disengagement."))
      return trf("HISTORY_WAR_RETREAT", {std::string(*name)},
                 "{0} began tactical disengagement.");
    if (const auto name = strip_suffix(summary, " successfully disengaged."))
      return trf("HISTORY_WAR_ESCAPED", {std::string(*name)},
                 "{0} successfully disengaged.");
    // "Engagement between {a} and {b} ended."
    if (const auto rest = strip_prefix(summary, "Engagement between "))
      if (const auto p = divide(*rest, " and "))
        if (const auto name = strip_suffix(p->second, " ended."))
          return trf("HISTORY_WAR_ENDED",
                     {std::string(p->first), std::string(*name)},
                     "Engagement between {0} and {1} ended.");
    static const std::pair<std::string_view, std::string_view> statics[] = {
        {"Tactical encounter concluded. Damage and losses are persistent.",
         "HISTORY_WAR_CONCLUDED"},
        {"An observed formation acted against an unidentified contact.",
         "HISTORY_WAR_OBSERVED_ACTION"},
        {"An unidentified hostile action affected a friendly formation.",
         "HISTORY_WAR_OBSERVED_HIT"},
    };
    for (const auto &[literal, key] : statics)
      if (summary == literal) return tr(key, literal);
    return std::string(summary);
  }

  if (category == "colony.founded") {
    // "{civ} established a sealed staffed resource outpost on {body} with
    //  {n} million specialist personnel."
    if (const auto p = divide(
            summary,
            " established a sealed staffed resource outpost on "))
      if (const auto q = divide(p->second, " with "))
        if (const auto amount = strip_suffix(
                q->second, " million specialist personnel."))
          return trf("HISTORY_COLONY_OUTPOST",
                     {std::string(p->first), std::string(q->first),
                      std::string(*amount)},
                     "{0} established a sealed staffed resource outpost on "
                     "{1} with {2} million specialist personnel.");
    // "{civ} established {colony} on {body} with {n} million {species}
    //  colonists using {natural environmental viability|prototype
    //  habitat support}."
    if (const auto p = divide(summary, " established "))
      if (const auto q = divide(p->second, " on "))
        if (const auto r = divide(q->second, " with "))
          if (const auto s = divide(r->second, " million "))
            if (const auto t = divide(s->second, " colonists using ")) {
              const auto method = t->second;
              if (method == "natural environmental viability.")
                return trf("HISTORY_COLONY_NATURAL",
                           {std::string(p->first), std::string(q->first),
                            std::string(r->first), std::string(s->first),
                            std::string(t->first)},
                           "{0} established {1} on {2} with {3} million {4} "
                           "colonists using natural environmental "
                           "viability.");
              if (method == "prototype habitat support.")
                return trf("HISTORY_COLONY_HABITAT",
                           {std::string(p->first), std::string(q->first),
                            std::string(r->first), std::string(s->first),
                            std::string(t->first)},
                           "{0} established {1} on {2} with {3} million {4} "
                           "colonists using prototype habitat support.");
            }
    return std::string(summary);
  }

  if (category.starts_with("exploration.")) {
    const auto fleet_body = [&](std::string_view key, std::string_view infix,
                                std::string_view suffix,
                                std::string_view fallback) -> std::string {
      if (const auto p = divide(summary, infix))
        if (const auto body = strip_suffix(p->second, suffix))
          return trf(key, {std::string(p->first), std::string(*body)},
                     std::string(fallback));
      return std::string(summary);
    };
    if (const auto out = fleet_body(
            "HISTORY_EXP_RESOURCE_SIG",
            " detected an unusual resource signature near ",
            "; detailed survey is required to confirm it.",
            "{0} detected an unusual resource signature near {1}; detailed "
            "survey is required to confirm it.");
        out != summary)
      return out;
    if (const auto out = fleet_body(
            "HISTORY_EXP_ANOMALY_SIG", " detected an anomalous signature near ",
            "; its nature remains unconfirmed.",
            "{0} detected an anomalous signature near {1}; its nature "
            "remains unconfirmed.");
        out != summary)
      return out;
    if (const auto out = fleet_body(
            "HISTORY_EXP_ACTIVITY_SIG",
            " detected unresolved activity signatures from ",
            "; detailed survey is required before classification.",
            "{0} detected unresolved activity signatures from {1}; detailed "
            "survey is required before classification.");
        out != summary)
      return out;
    if (const auto out = fleet_body("HISTORY_EXP_ANOMALY",
                                    " confirmed an anomaly on or near ", ".",
                                    "{0} confirmed an anomaly on or near "
                                    "{1}.");
        out != summary)
      return out;
    if (const auto out = fleet_body(
            "HISTORY_EXP_RESOURCE",
            " confirmed a rare-resource deposit or signature associated with ",
            ".", "{0} confirmed a rare-resource deposit or signature "
                 "associated with {1}.");
        out != summary)
      return out;
    if (const auto out = fleet_body(
            "HISTORY_EXP_NATIVE",
            " confirmed a native pre-warp civilization on ", ".",
            "{0} confirmed a native pre-warp civilization on {1}.");
        out != summary)
      return out;
    // "First contact: {name}."
    if (const auto rest = strip_prefix(summary, "First contact: "))
      if (const auto name = strip_suffix(*rest, "."))
        return trf("HISTORY_EXP_FIRST_CONTACT", {std::string(*name)},
                   "First contact: {0}.");
    // "{fleet} completed a rapid reconnaissance pass of {system};
    //  estimated detailed survey effort is {d} days ({hazard} survey
    //  conditions)."
    if (const auto p =
            divide(summary, " completed a rapid reconnaissance pass of "))
      if (const auto q =
              divide(p->second, "; estimated detailed survey effort is "))
        if (const auto r = divide(q->second, " days ("))
          if (const auto hazard = strip_suffix(r->second, " survey conditions).")) {
            static const std::pair<std::string_view, std::string_view> hazards[] =
                {{"routine", "HISTORY_HAZARD_ROUTINE"},
                 {"elevated", "HISTORY_HAZARD_ELEVATED"},
                 {"severe", "HISTORY_HAZARD_SEVERE"}};
            std::string condition{*hazard};
            for (const auto &[literal, key] : hazards)
              if (*hazard == literal) {
                condition = tr(key, literal);
                break;
              }
            return trf("HISTORY_EXP_RECON",
                       {std::string(p->first), std::string(q->first),
                        std::string(r->first), condition},
                       "{0} completed a rapid reconnaissance pass of {1}; "
                       "estimated detailed survey effort is {2} days ({3} "
                       "survey conditions).");
          }
    if (const auto p = divide(summary, " began a detailed science survey of "))
      if (const auto q = divide(p->second, "; estimated total effort is "))
        if (const auto days = strip_suffix(q->second, " days."))
          return trf("HISTORY_EXP_SURVEY_STARTED",
                     {std::string(p->first), std::string(q->first),
                      std::string(*days)},
                     "{0} began a detailed science survey of {1}; estimated "
                     "total effort is {2} days.");
    if (const auto out =
            fleet_body("HISTORY_EXP_SURVEYED", " completed a detailed survey of ",
                       ".", "{0} completed a detailed survey of {1}.");
        out != summary)
      return out;
    // "{fleet} reached astronomical target {n}; detailed system data
    //  still requires survey work."
    if (const auto p = divide(summary, " reached astronomical target "))
      if (const auto n = strip_suffix(
              p->second, "; detailed system data still requires survey work."))
        return trf("HISTORY_EXP_TARGET",
                   {std::string(p->first), std::string(*n)},
                   "{0} reached astronomical target {1}; detailed system "
                   "data still requires survey work.");
    // "Sensors added {n} system(s) to the local chart."
    if (const auto rest = strip_prefix(summary, "Sensors added "))
      if (const auto count = strip_suffix(*rest, " to the local chart.")) {
        if (const auto n = strip_suffix(*count, " systems"))
          return trf("HISTORY_EXP_SENSOR", {std::string(*n)},
                     "Sensors added {0} systems to the local chart.");
        if (const auto n = strip_suffix(*count, " system"))
          return trf("HISTORY_EXP_SENSOR_ONE", {std::string(*n)},
                     "Sensors added {0} system to the local chart.");
      }
    return std::string(summary);
  }

  // "{actor} completed {project|fleet|technology}." — the identical
  // completion skeleton across construction, shipbuilding and legacy
  // research journals.
  if (category == "construction.project" ||
      category == "shipbuilding.ship" || category == "research.legacy") {
    if (const auto p = divide(summary, " completed "))
      if (const auto name = strip_suffix(p->second, "."))
        return trf("HISTORY_COMPLETED",
                   {std::string(p->first), std::string(*name)},
                   "{0} completed {1}.");
    return std::string(summary);
  }

  if (category == "research.adaptive") {
    // "Research funding shortfall: {node} is operating at {pct}; progress
    //  is reduced until funding recovers."
    if (const auto rest = strip_prefix(summary, "Research funding shortfall: "))
      if (const auto p = divide(*rest, " is operating at "))
        if (const auto pct = strip_suffix(
                p->second,
                "; progress is reduced until funding recovers."))
          return trf("HISTORY_RESEARCH_SHORTFALL",
                     {std::string(p->first), std::string(*pct)},
                     "Research funding shortfall: {0} is operating at {1}; "
                     "progress is reduced until funding recovers.");
    if (const auto rest = strip_prefix(summary, "Research funding restored: "))
      if (const auto name =
              strip_suffix(*rest, " has resumed fully funded operations."))
        return trf("HISTORY_RESEARCH_RESTORED", {std::string(*name)},
                   "Research funding restored: {0} has resumed fully funded "
                   "operations.");
    // "Research milestone funded: {node} consumed {c}; {r} remains
    //  committed." / "Research milestone closed: {node} consumed its
    //  remaining {c} reserve during experimental resolution."
    if (const auto rest = strip_prefix(summary, "Research milestone funded: "))
      if (const auto p = divide(*rest, " consumed "))
        if (const auto q = divide(p->second, "; "))
          if (const auto remain = strip_suffix(q->second, " remains committed."))
            return trf("HISTORY_RESEARCH_MILESTONE",
                       {std::string(p->first), std::string(q->first),
                        std::string(*remain)},
                       "Research milestone funded: {0} consumed {1}; {2} "
                       "remains committed.");
    if (const auto rest = strip_prefix(summary, "Research milestone closed: "))
      if (const auto p = divide(*rest, " consumed its remaining "))
        if (const auto amount = strip_suffix(
                p->second,
                " reserve during experimental resolution."))
          return trf("HISTORY_RESEARCH_CLOSED",
                     {std::string(p->first), std::string(*amount)},
                     "Research milestone closed: {0} consumed its remaining "
                     "{1} reserve during experimental resolution.");
    // "Unexpected work made {node} immediately Investigable ..." /
    // "Unexpected work exposed {node} as a related hypothesis ..."
    if (const auto p = divide(summary, "Unexpected work made "))
      if (const auto name = strip_suffix(
              p->second, " immediately Investigable because its normal "
                         "requirements were already met."))
        return trf("HISTORY_RESEARCH_UNEXPECTED_READY",
                   {std::string(*name)},
                   "Unexpected work made {0} immediately Investigable "
                   "because its normal requirements were already met.");
    if (const auto p = divide(summary, "Unexpected work exposed "))
      if (const auto name = strip_suffix(
              p->second,
              " as a related hypothesis; normal requirements still govern "
              "researchability."))
        return trf("HISTORY_RESEARCH_UNEXPECTED",
                   {std::string(*name)},
                   "Unexpected work exposed {0} as a related hypothesis; "
                   "normal requirements still govern researchability.");
    if (const auto rest =
            strip_prefix(summary, "Research outcome resolved: "))
      if (const auto id = strip_suffix(*rest, "."))
        return trf("HISTORY_RESEARCH_RESOLVED", {std::string(*id)},
                   "Research outcome resolved: {0}.");
    // Runtime node-progression messages.
    if (const auto rest = strip_prefix(
            summary, "New research program is Investigable: "))
      if (const auto name = strip_suffix(*rest, "."))
        return trf("HISTORY_RESEARCH_INVESTIGABLE", {std::string(*name)},
                   "New research program is Investigable: {0}.");
    if (const auto rest =
            strip_prefix(summary, "Functional capability gained: "))
      if (const auto name = strip_suffix(*rest, "."))
        return trf("HISTORY_RESEARCH_CAPABILITY", {std::string(*name)},
                   "Functional capability gained: {0}.");
    if (const auto rest = strip_prefix(
            summary,
            "Research established applicability trait/capability context '"))
      if (const auto id = strip_suffix(*rest, "'."))
        return trf("HISTORY_RESEARCH_TRAIT", {std::string(*id)},
                   "Research established applicability trait/capability "
                   "context '{0}'.");
    if (const auto rest = strip_prefix(
            summary, "Directed research coordination advanced to '"))
      if (const auto id = strip_suffix(*rest, "'."))
        return trf("HISTORY_RESEARCH_DIRECTED", {std::string(*id)},
                   "Directed research coordination advanced to '{0}'.");
    if (const auto name = strip_suffix(
            summary, " reached Mature scientific/engineering knowledge."))
      return trf("HISTORY_RESEARCH_MATURE", {std::string(*name)},
                 "{0} reached Mature scientific/engineering knowledge.");
    if (const auto name = strip_suffix(
            summary, " reached its experimental evidence boundary and "
                     "requires scientific resolution."))
      return trf("HISTORY_RESEARCH_BOUNDARY", {std::string(*name)},
                 "{0} reached its experimental evidence boundary and "
                 "requires scientific resolution.");
    // "{node} advanced to {stage}." — stage is a maturity name.
    if (const auto p = divide(summary, " advanced to "))
      if (const auto stage = strip_suffix(p->second, ".")) {
        static const std::pair<std::string_view, std::string_view>
            maturities[] = {
                {"Rumored", "HISTORY_MATURITY_RUMORED"},
                {"Hypothesized", "HISTORY_MATURITY_HYPOTHESIZED"},
                {"Investigable", "HISTORY_MATURITY_INVESTIGABLE"},
                {"Experimental", "HISTORY_MATURITY_EXPERIMENTAL"},
                {"Demonstrated", "HISTORY_MATURITY_DEMONSTRATED"},
                {"Engineering", "HISTORY_MATURITY_ENGINEERING"},
                {"Mature", "HISTORY_MATURITY_MATURE"},
                {"Archived", "HISTORY_MATURITY_ARCHIVED"},
            };
        std::string name{*stage};
        for (const auto &[literal, key] : maturities)
          if (*stage == literal) {
            name = tr(key, literal);
            break;
          }
        return trf("HISTORY_RESEARCH_ADVANCED",
                   {std::string(p->first), name}, "{0} advanced to {1}.");
      }
    static const std::pair<std::string_view, std::string_view> statics[] = {
        {"A failed test created additional engineering work; total "
         "scientific work remains recorded.",
         "HISTORY_RESEARCH_SETBACK"},
        {"A limited/reduced-performance result worked and reduced "
         "remaining work without granting Mature technology.",
         "HISTORY_RESEARCH_PARTIAL"},
        {"The tested hypothesis was disproven and archived; negative "
         "knowledge and scientific practice are retained.",
         "HISTORY_RESEARCH_DISPROVEN"},
        {"The hypothesis survived only in revised form; most effective "
         "experimental progress was preserved for another test cycle.",
         "HISTORY_RESEARCH_REVISED"},
        {"A reproducible anomaly did not validate the expected model; it "
         "created a legitimate new fundamental-science signal.",
         "HISTORY_RESEARCH_ANOMALY"},
        {"A research hazard occurred. Adaptive Research reports the fact; "
         "physical damage/casualties are resolved by the owning subsystem.",
         "HISTORY_RESEARCH_HAZARD"},
    };
    for (const auto &[literal, key] : statics)
      if (summary == literal) return tr(key, literal);
    return std::string(summary);
  }

  return std::string(summary);
}
} // namespace stellar::native_history
