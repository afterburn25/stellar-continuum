#include <stellar/engine/scripted_content.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace stellar::engine {
namespace {

bool parse_int64(const nlohmann::json &value, std::int64_t &out) {
  if (value.is_number_integer()) { out = value.get<std::int64_t>(); return true; }
  if (value.is_string())
    try {
      std::size_t used{};
      const auto parsed = std::stoll(value.get<std::string>(), &used);
      if (used == value.get<std::string>().size()) { out = parsed; return true; }
    } catch (...) {}
  return false;
}

bool parse_double(const nlohmann::json &value, double &out) {
  if (value.is_number()) { out = value.get<double>(); return true; }
  if (value.is_string())
    try {
      std::size_t used{};
      const auto parsed = std::stod(value.get<std::string>(), &used);
      if (used == value.get<std::string>().size()) { out = parsed; return true; }
    } catch (...) {}
  return false;
}

bool parse_scope(const nlohmann::json &node, ScriptScope &scope, std::string &error) {
  static const std::unordered_map<std::string, ScriptScopeKind> kinds{
      {"global", ScriptScopeKind::global},
      {"civilization", ScriptScopeKind::civilization},
      {"colony", ScriptScopeKind::colony},
      {"system", ScriptScopeKind::system},
      {"body", ScriptScopeKind::body},
      {"fleet", ScriptScopeKind::fleet},
      {"origin", ScriptScopeKind::origin}};
  if (!node.is_object()) { error = "scope must be an object"; return false; }
  const auto kind = node.find("kind");
  if (kind == node.end() || !kind->is_string()) {
    error = "scope.kind must be a string"; return false;
  }
  const auto it = kinds.find(kind->get<std::string>());
  if (it == kinds.end()) { error = "unknown scope.kind '" + kind->get<std::string>() + "'"; return false; }
  scope.kind = it->second;
  scope.id = -1;
  if (const auto id = node.find("id"); id != node.end() && !parse_int64(*id, scope.id)) {
    error = "scope.id must be an integer"; return false;
  }
  return true;
}

bool parse_condition(const nlohmann::json &node, ScriptCondition &condition,
                     std::string &error, unsigned depth = 0) {
  if (depth > 16) { error = "trigger nesting exceeds 16 levels"; return false; }
  if (!node.is_object()) { error = "condition must be an object"; return false; }
  if (const auto all = node.find("all"); all != node.end()) {
    if (!all->is_array()) { error = "all must be an array"; return false; }
    condition.op = ScriptCondition::Op::all;
    for (const auto &child : *all) {
      ScriptCondition c;
      if (!parse_condition(child, c, error, depth + 1)) return false;
      condition.children.push_back(std::move(c));
    }
    return true;
  }
  if (const auto any = node.find("any"); any != node.end()) {
    if (!any->is_array()) { error = "any must be an array"; return false; }
    condition.op = ScriptCondition::Op::any;
    for (const auto &child : *any) {
      ScriptCondition c;
      if (!parse_condition(child, c, error, depth + 1)) return false;
      condition.children.push_back(std::move(c));
    }
    return true;
  }
  if (const auto negated = node.find("not"); negated != node.end()) {
    condition.op = ScriptCondition::Op::negation;
    ScriptCondition child;
    if (!parse_condition(*negated, child, error, depth + 1)) return false;
    condition.children.push_back(std::move(child));
    return true;
  }
  const auto check = node.find("check");
  if (check == node.end() || !check->is_string()) {
    error = "leaf condition needs a string 'check'"; return false;
  }
  condition.check = check->get<std::string>();
  if (condition.check.empty()) { error = "check must not be empty"; return false; }
  if (const auto scope = node.find("scope"); scope != node.end() &&
      !parse_scope(*scope, condition.scope, error))
    return false;
  if (const auto args = node.find("args"); args != node.end()) {
    if (!args->is_object()) { error = "args must be an object"; return false; }
    for (const auto &[key, value] : args->items()) {
      if (value.is_string()) condition.args.emplace_back(key, value.get<std::string>());
      else if (value.is_number_integer() || value.is_number_unsigned())
        condition.args.emplace_back(key, std::to_string(value.get<std::int64_t>()));
      else if (value.is_number_float())
        condition.args.emplace_back(key, std::to_string(value.get<double>()));
      else if (value.is_boolean())
        condition.args.emplace_back(key, value.get<bool>() ? "true" : "false");
      else { error = "args." + key + " must be a scalar"; return false; }
    }
  }
  return true;
}

bool parse_effects(const nlohmann::json &node,
                   std::vector<ScriptEffect> &effects, std::string &error) {
  if (!node.is_array()) { error = "effects must be an array"; return false; }
  for (const auto &entry : node) {
    if (!entry.is_object()) { error = "effect must be an object"; return false; }
    ScriptEffect effect;
    const auto action = entry.find("do");
    if (action == entry.end() || !action->is_string() || action->get<std::string>().empty()) {
      error = "effect needs a non-empty string 'do'"; return false;
    }
    effect.action = action->get<std::string>();
    if (const auto scope = entry.find("scope"); scope != entry.end() &&
        !parse_scope(*scope, effect.scope, error))
      return false;
    if (const auto args = entry.find("args"); args != entry.end()) {
      if (!args->is_object()) { error = "effect args must be an object"; return false; }
      for (const auto &[key, value] : args->items()) {
        if (value.is_string()) effect.args.emplace_back(key, value.get<std::string>());
        else if (value.is_number()) effect.args.emplace_back(key, value.dump());
        else if (value.is_boolean())
          effect.args.emplace_back(key, value.get<bool>() ? "true" : "false");
        else { error = "effect args." + key + " must be a scalar"; return false; }
      }
    }
    effects.push_back(std::move(effect));
  }
  return true;
}

std::optional<double>
condition_number_arg(const ScriptCondition &condition, std::string_view key) {
  for (const auto &[name, value] : condition.args)
    if (name == key) {
      double parsed{};
      try {
        std::size_t used{};
        parsed = std::stod(value, &used);
        if (used != value.size()) return std::nullopt;
      } catch (...) { return std::nullopt; }
      return parsed;
    }
  return std::nullopt;
}

// Leaf checks evaluated inside the runtime (deterministic roll stream) —
// the adapter never sees them, and the runtime validates them locally.
bool is_reserved_check(std::string_view check) { return check == "random_chance"; }

std::string validate_reserved_check(const ScriptCondition &leaf) {
  if (leaf.check == "random_chance") {
    const auto probability = condition_number_arg(leaf, "probability");
    if (!probability || *probability < 0.0 || *probability > 1.0)
      return "random_chance needs args.probability in [0,1]";
    return {};
  }
  return "unknown reserved check";
}

void validate_condition_tree(const ScriptCondition &condition,
                             ScriptedContentAdapter *adapter,
                             std::string_view file, std::string_view object_id,
                             std::vector<ScriptLoadError> &errors) {
  if (condition.op == ScriptCondition::Op::leaf) {
    if (is_reserved_check(condition.check)) {
      if (auto message = validate_reserved_check(condition); !message.empty())
        errors.push_back({std::string(file), std::string(object_id), std::move(message)});
      return;
    }
    if (adapter) {
      if (auto message = adapter->validate_condition(condition, file, object_id);
          !message.empty())
        errors.push_back({std::string(file), std::string(object_id), std::move(message)});
    }
    return;
  }
  for (const auto &child : condition.children)
    validate_condition_tree(child, adapter, file, object_id, errors);
}

// Cycle detection over the follow-up graph — a delayed self-reference is a
// recursive event loop that would fire forever.
bool follow_up_cycle(const std::unordered_map<std::string, std::size_t> &index,
                     const std::vector<ScriptedEventDefinition> &definitions,
                     std::size_t at, std::unordered_set<std::size_t> &visiting,
                     std::unordered_set<std::size_t> &done) {
  if (done.count(at)) return false;
  if (!visiting.insert(at).second) return true;
  for (const auto &next : definitions[at].follow_ups) {
    const auto it = index.find(next.event_id);
    if (it != index.end() && follow_up_cycle(index, definitions, it->second,
                                             visiting, done))
      return true;
  }
  visiting.erase(at);
  done.insert(at);
  return false;
}

} // namespace

std::optional<ScriptedEventDefinition>
ScriptedEventDefinition::parse(std::string_view json_object, std::string_view file,
                               std::string *error) {
  std::string failure;
  std::string &err = error ? *error : failure;
  auto node = nlohmann::json::parse(json_object, nullptr, false);
  if (node.is_discarded() || !node.is_object()) {
    err = "event definition must be a JSON object"; return std::nullopt;
  }
  ScriptedEventDefinition definition;
  definition.file = std::string(file);
  const auto id = node.find("id");
  if (id == node.end() || !id->is_string() || id->get<std::string>().empty()) {
    err = "event needs a non-empty string 'id'"; return std::nullopt;
  }
  definition.id = id->get<std::string>();

  const auto on = node.find("on");
  if (on != node.end()) {
    if (!on->is_string() || on->get<std::string>().empty()) {
      err = "on must be a non-empty event name"; return std::nullopt;
    }
    definition.on = on->get<std::string>();
  }
  const auto poll = node.find("poll_days");
  if (poll != node.end() &&
      (!parse_double(*poll, definition.poll_days) || definition.poll_days <= 0)) {
    err = "poll_days must be a positive number"; return std::nullopt;
  }
  if (!definition.on.empty() && definition.poll_days > 0) {
    err = "'on' and 'poll_days' are mutually exclusive firing modes";
    return std::nullopt;
  }
  if (const auto trigger = node.find("trigger"); trigger != node.end()) {
    if (!parse_condition(*trigger, definition.trigger, err)) return std::nullopt;
    definition.has_trigger = true;
  }
  if (definition.poll_days > 0 && !definition.has_trigger) {
    err = "poll_days requires a trigger to evaluate"; return std::nullopt;
  }
  if (const auto weight = node.find("weight"); weight != node.end() &&
      (!parse_double(*weight, definition.weight) || !(definition.weight > 0))) {
    err = "weight must be a positive number"; return std::nullopt;
  }
  if (const auto once = node.find("once_per_campaign"); once != node.end()) {
    if (!once->is_boolean()) { err = "once_per_campaign must be boolean"; return std::nullopt; }
    definition.once_per_campaign = once->get<bool>();
  }
  if (const auto once = node.find("once_per_scope"); once != node.end()) {
    if (!once->is_boolean()) { err = "once_per_scope must be boolean"; return std::nullopt; }
    definition.once_per_scope = once->get<bool>();
  }
  if (definition.once_per_campaign && definition.once_per_scope) {
    err = "'once_per_campaign' and 'once_per_scope' are mutually exclusive";
    return std::nullopt;
  }
  if (const auto cooldown = node.find("cooldown_days"); cooldown != node.end() &&
      (!parse_double(*cooldown, definition.cooldown_days) || definition.cooldown_days < 0)) {
    err = "cooldown_days must be a non-negative number"; return std::nullopt;
  }
  if (const auto scope = node.find("scope"); scope != node.end() &&
      !parse_scope(*scope, definition.scope, err))
    return std::nullopt;
  if (const auto effects = node.find("effects"); effects != node.end() &&
      !parse_effects(*effects, definition.effects, err))
    return std::nullopt;
  if (const auto follow = node.find("follow_ups"); follow != node.end()) {
    if (!follow->is_array()) { err = "follow_ups must be an array"; return std::nullopt; }
    for (const auto &entry : *follow) {
      if (!entry.is_object()) { err = "follow_up must be an object"; return std::nullopt; }
      ScriptFollowUp next;
      const auto ref = entry.find("id");
      if (ref == entry.end() || !ref->is_string() || ref->get<std::string>().empty()) {
        err = "follow_up needs a non-empty string 'id'"; return std::nullopt;
      }
      next.event_id = ref->get<std::string>();
      if (const auto delay = entry.find("delay_days"); delay != entry.end() &&
          (!parse_double(*delay, next.delay_days) || next.delay_days < 0)) {
        err = "follow_up delay_days must be a non-negative number"; return std::nullopt;
      }
      if (const auto w = entry.find("weight"); w != entry.end() &&
          (!parse_double(*w, next.weight) || !(next.weight > 0))) {
        err = "follow_up weight must be a positive number"; return std::nullopt;
      }
      definition.follow_ups.push_back(std::move(next));
    }
  }
  return definition;
}

const ScriptedEventDefinition *
ScriptedContentRuntime::definition(std::string_view id) const {
  for (const auto &entry : definitions_)
    if (entry.id == id) return &entry;
  return nullptr;
}

bool ScriptedContentRuntime::load_document(std::string_view json_document,
                                           std::string_view file,
                                           std::vector<ScriptLoadError> *errors) {
  std::vector<ScriptLoadError> local;
  std::vector<ScriptLoadError> &out = errors ? *errors : local;
  const auto record = [&](std::string_view object_id, std::string message) {
    out.push_back({std::string(file), std::string(object_id), std::move(message)});
  };

  const auto document = nlohmann::json::parse(json_document, nullptr, false);
  if (document.is_discarded() || !document.is_object()) {
    record("", "document must be a JSON object");
    return false;
  }
  const auto events = document.find("scripted_events");
  if (events == document.end() || !events->is_array()) {
    record("", "document needs a scripted_events array");
    return false;
  }

  // Parse into a staging list — load is atomic: nothing mutates the registry
  // until the whole document validates.
  std::vector<ScriptedEventDefinition> staged;
  std::unordered_set<std::string> staged_ids;
  for (const auto &entry : *events) {
    std::string parse_error;
    auto parsed = ScriptedEventDefinition::parse(entry.dump(), file, &parse_error);
    if (!parsed) { record("", parse_error); continue; }
    if (!staged_ids.insert(parsed->id).second ||
        definition(parsed->id) != nullptr) {
      record(parsed->id, "duplicate scripted event id");
      continue;
    }
    if (adapter_) {
      if (parsed->has_trigger)
        validate_condition_tree(parsed->trigger, adapter_, file, parsed->id, out);
      for (const auto &effect : parsed->effects)
        if (auto message = adapter_->validate_effect(effect, file, parsed->id);
            !message.empty())
          record(parsed->id, std::move(message));
    }
    staged.push_back(std::move(*parsed));
  }

  // Follow-up references must resolve within the merged definition set.
  for (const auto &event : staged)
    for (const auto &next : event.follow_ups) {
      const bool known = staged_ids.count(next.event_id) ||
                         definition(next.event_id) != nullptr;
      if (!known)
        record(event.id, "follow_up references unknown event '" + next.event_id + "'");
    }
  if (!out.empty()) return false;

  // Recursive-event detection across merged definitions (existing + staged).
  std::vector<ScriptedEventDefinition> merged = definitions_;
  merged.insert(merged.end(), staged.begin(), staged.end());
  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t i = 0; i < merged.size(); ++i) index.emplace(merged[i].id, i);
  std::unordered_set<std::size_t> visiting, done;
  for (std::size_t i = 0; i < merged.size(); ++i)
    if (!done.count(i) && follow_up_cycle(index, merged, i, visiting, done)) {
      record(merged[i].id, "follow_up chain forms a recursive event loop");
      return false;
    }
  if (!out.empty()) return false;

  definitions_.insert(definitions_.end(),
                      std::make_move_iterator(staged.begin()),
                      std::make_move_iterator(staged.end()));
  return true;
}

void ScriptedContentRuntime::clear() {
  definitions_.clear();
  scheduled_.clear();
  fired_once_.clear();
  cooldown_until_.clear();
}

std::string
ScriptedContentRuntime::once_key(const ScriptedEventDefinition &definition,
                                 const ScriptFiringContext &context) const {
  if (!definition.once_per_scope) return definition.id;
  const auto resolved = resolve_scope_id(definition.scope, context);
  if (resolved < 0) return definition.id;
  return definition.id + "@" + std::to_string(resolved);
}

bool ScriptedContentRuntime::fired_key(std::string_view key) const {
  return std::find(fired_once_.begin(), fired_once_.end(), key) !=
         fired_once_.end();
}

bool ScriptedContentRuntime::fired(std::string_view event_id) const {
  if (fired_key(event_id)) return true;
  // once_per_scope entries persist as "<id>@<scope>" — an unscoped query
  // reports whether the event ever fired anywhere.
  for (const auto &key : fired_once_)
    if (key.size() > event_id.size() && key.starts_with(event_id) &&
        key[event_id.size()] == '@')
      return true;
  return false;
}

double ScriptedContentRuntime::cooldown_remaining(std::string_view event_id,
                                                  double simulation_day) const {
  for (const auto &[id, until] : cooldown_until_)
    if (id == event_id)
      return until > simulation_day ? until - simulation_day : 0.0;
  return 0.0;
}

std::uint64_t ScriptedContentRuntime::next_roll() noexcept {
  // splitmix64 — deterministic, persisted as roll_state_.
  std::uint64_t z = (roll_state_ += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

double ScriptedContentRuntime::roll_unit() noexcept {
  return (next_roll() >> 11) * (1.0 / 9007199254740992.0);
}

std::int64_t
ScriptedContentRuntime::resolve_scope_id(const ScriptScope &scope,
                                         const ScriptFiringContext &context) const {
  if (scope.id >= 0) return scope.id;
  switch (scope.kind) {
  case ScriptScopeKind::global: return -1;
  case ScriptScopeKind::civilization: return context.civilization;
  case ScriptScopeKind::colony: return context.colony;
  case ScriptScopeKind::system: return context.system;
  case ScriptScopeKind::body: return context.body;
  case ScriptScopeKind::fleet: return context.fleet;
  case ScriptScopeKind::origin: return context.origin;
  }
  return -1;
}

bool ScriptedContentRuntime::evaluate(const ScriptCondition &condition,
                                      const ScriptFiringContext &context) {
  switch (condition.op) {
  case ScriptCondition::Op::all:
    return std::all_of(condition.children.begin(), condition.children.end(),
                       [&](const ScriptCondition &child) {
                         return evaluate(child, context);
                       });
  case ScriptCondition::Op::any:
    return std::any_of(condition.children.begin(), condition.children.end(),
                       [&](const ScriptCondition &child) {
                         return evaluate(child, context);
                       });
  case ScriptCondition::Op::negation:
    return !condition.children.empty() &&
           !evaluate(condition.children.front(), context);
  case ScriptCondition::Op::leaf:
    if (is_reserved_check(condition.check)) {
      // Consumes the persisted roll stream — deterministic across save/load.
      const auto probability = condition_number_arg(condition, "probability");
      return probability && roll_unit() < *probability;
    }
    return adapter_ && adapter_->evaluate_condition(condition, context);
  }
  return false;
}

void ScriptedContentRuntime::run_effects(
    const ScriptedEventDefinition &definition,
    const ScriptFiringContext &context) {
  for (const auto &effect : definition.effects)
    adapter_->apply_effect(effect, context);
  adapter_->event_fired(definition, context);
}

void ScriptedContentRuntime::handle_event(std::string_view event_name,
                                          const ScriptFiringContext &context,
                                          double simulation_day) {
  if (!adapter_) return;
  if (simulation_day >= 0) now_ = simulation_day;
  ScriptFiringContext bound = context;
  bound.event_name = event_name;
  // Collect eligible definitions by their declared weight so one firing
  // context picks a single deterministic winner (plus unlimited on-events
  // with weight<=0 are disallowed at parse time, so all weighted entries
  // compete in one roll).
  double total = 0.0;
  for (const auto &entry : definitions_)
    if (entry.on == event_name) total += entry.weight;
  if (total <= 0) return;
  std::vector<const ScriptedEventDefinition *> eligible;
  double eligible_weight = 0.0;
  for (const auto &entry : definitions_) {
    if (entry.on != event_name) continue;
    if ((entry.once_per_campaign || entry.once_per_scope) &&
        fired_key(once_key(entry, bound)))
      continue;
    if (entry.has_trigger && !evaluate(entry.trigger, bound)) continue;
    eligible.push_back(&entry);
    eligible_weight += entry.weight;
  }
  if (eligible.empty()) return;
  const double pick = roll_unit() * eligible_weight;
  double cursor = 0.0;
  const ScriptedEventDefinition *winner = eligible.back();
  for (const auto *entry : eligible) {
    cursor += entry->weight;
    if (pick < cursor) { winner = entry; break; }
  }
  fire(winner->id, bound, /*force=*/true);
}

void ScriptedContentRuntime::advance(double simulation_day) {
  if (!adapter_) return;
  now_ = simulation_day;
  // Due scheduled follow-ups fire in day order.
  std::vector<ScheduledScriptedEvent> remaining;
  for (auto &entry : scheduled_) {
    if (entry.fire_day <= simulation_day)
      fire(entry.event_id, entry.context, /*force=*/true);
    else
      remaining.push_back(std::move(entry));
  }
  scheduled_ = std::move(remaining);

  // Cadence-polled definitions: evaluate their trigger against every context
  // the adapter enumerates; eligible contexts compete in one weighted roll.
  for (const auto &entry : definitions_) {
    if (entry.poll_days <= 0) continue;
    if (entry.once_per_campaign && fired(entry.id)) continue;
    if (cooldown_remaining(entry.id, simulation_day) > 0) continue;
    auto due = std::find_if(poll_due_.begin(), poll_due_.end(),
                            [&](const auto &row) { return row.first == entry.id; });
    if (due == poll_due_.end()) {
      poll_due_.emplace_back(entry.id, simulation_day + entry.poll_days);
      due = poll_due_.end() - 1;
      // First poll evaluates on arrival — a freshly loaded pack reports
      // immediately rather than waiting a full cadence.
    } else if (simulation_day < due->second) {
      continue;
    } else {
      due->second = simulation_day + entry.poll_days;
    }
    const auto contexts = adapter_->enumerate_contexts(entry);
    double eligible_weight = 0.0;
    std::vector<const ScriptFiringContext *> eligible;
    for (const auto &context : contexts) {
      if (entry.once_per_scope && fired_key(once_key(entry, context)))
        continue;
      if (!evaluate(entry.trigger, context)) continue;
      eligible.push_back(&context);
      eligible_weight += entry.weight;
    }
    if (eligible.empty()) continue;
    const double pick = roll_unit() * eligible_weight;
    double cursor = 0.0;
    const ScriptFiringContext *winner = eligible.back();
    for (const auto *context : eligible) {
      cursor += entry.weight;
      if (pick < cursor) { winner = context; break; }
    }
    fire(entry.id, *winner, /*force=*/true);
    if (entry.cooldown_days > 0)
      cooldown_until_.push_back({entry.id, simulation_day + entry.cooldown_days});
  }
}

bool ScriptedContentRuntime::fire(std::string_view event_id,
                                  const ScriptFiringContext &context,
                                  bool force) {
  const auto *entry = definition(event_id);
  if (!entry || !adapter_) return false;
  const std::string key = once_key(*entry, context);
  if ((entry->once_per_campaign || entry->once_per_scope) && fired_key(key))
    return false;
  if (!force && entry->has_trigger && !evaluate(entry->trigger, context))
    return false;
  run_effects(*entry, context);
  if (entry->once_per_campaign || entry->once_per_scope)
    fired_once_.push_back(key);
  // Follow-ups schedule with their declared delays; a weighted pick runs when
  // several entries compete for one slot.
  if (!entry->follow_ups.empty()) {
    double total = 0.0;
    for (const auto &next : entry->follow_ups) total += next.weight;
    const double pick = roll_unit() * total;
    double cursor = 0.0;
    for (const auto &next : entry->follow_ups) {
      cursor += next.weight;
      if (pick <= cursor) {
        // Scheduled events remember the firing day through the context's
        // host — the caller stamps the day via advance(); use the largest
        // known day base by storing the delay in fire_day for the caller.
        ScheduledScriptedEvent scheduled;
        scheduled.event_id = next.event_id;
        scheduled.context = context;
        scheduled.context.event_name = {};
        scheduled.fire_day = now_ + next.delay_days;
        scheduled_.push_back(std::move(scheduled));
        break;
      }
    }
  }
  return true;
}

namespace {
struct ContextJson {
  static nlohmann::json to(const ScriptFiringContext &context) {
    return {{"civilization", context.civilization},
            {"colony", context.colony},
            {"system", context.system},
            {"body", context.body},
            {"fleet", context.fleet},
            {"origin", context.origin}};
  }
  static ScriptFiringContext from(const nlohmann::json &node) {
    ScriptFiringContext context;
    context.civilization = node.value("civilization", -1);
    context.colony = node.value("colony", -1);
    context.system = node.value("system", -1);
    context.body = node.value("body", -1);
    context.fleet = node.value("fleet", -1);
    context.origin = node.value("origin", -1);
    return context;
  }
};
} // namespace

std::string ScriptedContentRuntime::serialize() const {
  nlohmann::json document;
  document["format_version"] = 1;
  document["roll_state"] = roll_state_;
  document["now"] = now_;
  document["fired_once"] = fired_once_;
  document["cooldowns"] = nlohmann::json::array();
  for (const auto &[id, until] : cooldown_until_)
    document["cooldowns"].push_back({{"id", id}, {"until", until}});
  document["poll_due"] = nlohmann::json::array();
  for (const auto &[id, day] : poll_due_)
    document["poll_due"].push_back({{"id", id}, {"day", day}});
  document["scheduled"] = nlohmann::json::array();
  for (const auto &entry : scheduled_)
    document["scheduled"].push_back({{"id", entry.event_id},
                                     {"fire_day", entry.fire_day},
                                     {"context", ContextJson::to(entry.context)}});
  return document.dump();
}

bool ScriptedContentRuntime::restore(std::string_view document,
                                     std::string *error) {
  const auto parsed = nlohmann::json::parse(document, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    if (error) *error = "scripted state must be a JSON object";
    return false;
  }
  // Absent version means the pre-versioned writer (also shape 1); a future
  // incompatible schema bumps the version and migrates here.
  if (const auto version = parsed.find("format_version"); version != parsed.end()) {
    if (!version->is_number_integer() || version->get<int>() != 1) {
      if (error) *error = "unsupported scripted state format_version";
      return false;
    }
  }
  const auto get_u64 = [](const nlohmann::json &node, const char *key,
                          std::uint64_t &out) {
    const auto it = node.find(key);
    return it != node.end() && it->is_number_unsigned() &&
           (out = it->get<std::uint64_t>(), true);
  };
  std::uint64_t roll{};
  if (!get_u64(parsed, "roll_state", roll) || roll == 0) {
    if (error) *error = "missing roll_state";
    return false;
  }
  const double restored_now =
      parsed.contains("now") && parsed["now"].is_number()
          ? parsed["now"].get<double>()
          : 0.0;
  std::vector<std::string> fired;
  if (const auto list = parsed.find("fired_once"); list != parsed.end()) {
    if (!list->is_array()) { if (error) *error = "fired_once must be an array"; return false; }
    for (const auto &entry : *list)
      if (!entry.is_string()) { if (error) *error = "fired_once entries must be strings"; return false; }
      else fired.push_back(entry.get<std::string>());
  }
  std::vector<std::pair<std::string, double>> cooldowns;
  if (const auto list = parsed.find("cooldowns"); list != parsed.end()) {
    if (!list->is_array()) { if (error) *error = "cooldowns must be an array"; return false; }
    for (const auto &entry : *list) {
      if (!entry.is_object() || !entry["id"].is_string() ||
          !entry.contains("until") || !entry["until"].is_number()) {
        if (error) *error = "cooldown entry malformed"; return false;
      }
      cooldowns.emplace_back(entry["id"].get<std::string>(),
                             entry["until"].get<double>());
    }
  }
  std::vector<std::pair<std::string, double>> poll_due;
  if (const auto list = parsed.find("poll_due"); list != parsed.end()) {
    if (!list->is_array()) { if (error) *error = "poll_due must be an array"; return false; }
    for (const auto &entry : *list) {
      if (!entry.is_object() || !entry["id"].is_string() ||
          !entry.contains("day") || !entry["day"].is_number()) {
        if (error) *error = "poll_due entry malformed"; return false;
      }
      poll_due.emplace_back(entry["id"].get<std::string>(),
                            entry["day"].get<double>());
    }
  }
  std::vector<ScheduledScriptedEvent> scheduled;
  if (const auto list = parsed.find("scheduled"); list != parsed.end()) {
    if (!list->is_array()) { if (error) *error = "scheduled must be an array"; return false; }
    for (const auto &entry : *list) {
      if (!entry.is_object() || !entry["id"].is_string() ||
          !entry.contains("fire_day") || !entry["fire_day"].is_number() ||
          !entry["context"].is_object()) {
        if (error) *error = "scheduled entry malformed"; return false;
      }
      ScheduledScriptedEvent item;
      item.event_id = entry["id"].get<std::string>();
      item.fire_day = entry["fire_day"].get<double>();
      item.context = ContextJson::from(entry["context"]);
      scheduled.push_back(std::move(item));
    }
  }
  roll_state_ = roll;
  now_ = restored_now;
  fired_once_ = std::move(fired);
  cooldown_until_ = std::move(cooldowns);
  poll_due_ = std::move(poll_due);
  scheduled_ = std::move(scheduled);
  return true;
}

} // namespace stellar::engine
