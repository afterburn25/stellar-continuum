#include <stellar/engine/mission_graph.hpp>

#include <stellar/engine/event_bus.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace stellar::engine {
namespace {

// Resolves "a.b.c" against a JSON payload; returns the scalar as a string.
std::optional<std::string> field_value(const nlohmann::json &payload,
                                       std::string_view path) {
  const nlohmann::json *node = &payload;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto dot = path.find('.', start);
    const auto part = path.substr(
        start, dot == std::string_view::npos ? std::string_view::npos
                                             : dot - start);
    if (part.empty() || !node->is_object() ||
        !node->contains(std::string(part)))
      return std::nullopt;
    node = &node->at(std::string(part));
    if (dot == std::string_view::npos)
      break;
    start = dot + 1;
  }
  if (node->is_string())
    return node->get<std::string>();
  if (node->is_number_integer())
    return std::to_string(node->get<std::int64_t>());
  if (node->is_number_unsigned())
    return std::to_string(node->get<std::uint64_t>());
  if (node->is_number_float()) {
    std::ostringstream out;
    out << node->get<double>();
    return out.str();
  }
  if (node->is_boolean())
    return node->get<bool>() ? std::string("true") : std::string("false");
  return std::nullopt;
}

std::string json_string(const nlohmann::json &value,
                        const std::string &fallback = {}) {
  return value.is_string() ? value.get<std::string>() : fallback;
}

} // namespace

std::optional<MissionDefinition>
MissionDefinition::parse(std::string_view json_document, std::string *error) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_document);
  } catch (const std::exception &ex) {
    if (error != nullptr)
      *error = ex.what();
    return std::nullopt;
  }
  auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return std::nullopt;
  };
  if (!doc.is_object())
    return fail("mission definition must be an object");
  MissionDefinition definition;
  definition.id = doc.value("id", std::string{});
  if (definition.id.empty())
    return fail("mission requires an 'id'");
  for (const auto &entry : doc.value("triggers", nlohmann::json::array())) {
    MissionTrigger trigger;
    trigger.event = json_string(entry.value("event", nlohmann::json{}));
    trigger.stage = json_string(entry.value("stage", nlohmann::json{}));
    if (trigger.event.empty() || trigger.stage.empty())
      return fail("trigger requires 'event' and 'stage'");
    for (const auto &cond :
         entry.value("conditions", nlohmann::json::array())) {
      MissionCondition condition;
      condition.field = json_string(cond.value("field", nlohmann::json{}));
      condition.equals = json_string(cond.value("equals", nlohmann::json{}));
      if (cond.contains("equals") && !cond.at("equals").is_string()) {
        const auto scalar = cond.at("equals");
        if (scalar.is_number_integer())
          condition.equals = std::to_string(scalar.get<std::int64_t>());
        else if (scalar.is_number_float()) {
          std::ostringstream out;
          out << scalar.get<double>();
          condition.equals = out.str();
        } else if (scalar.is_boolean())
          condition.equals = scalar.get<bool>() ? "true" : "false";
      }
      if (condition.field.empty())
        return fail("condition requires a 'field'");
      trigger.conditions.push_back(std::move(condition));
    }
    definition.triggers.push_back(std::move(trigger));
  }
  const auto stages = doc.value("stages", nlohmann::json::object());
  for (const auto &[stage_id, stage_json] : stages.items()) {
    if (!stage_json.is_object())
      return fail("stage '" + stage_id + "' must be an object");
    MissionStage stage;
    stage.id = stage_id;
    stage.title_key =
        json_string(stage_json.value("title_key", nlohmann::json{}));
    stage.body_key =
        json_string(stage_json.value("body_key", nlohmann::json{}));
    stage.timer_days = stage_json.value("timer_days", 0.0);
    stage.timeout_stage =
        json_string(stage_json.value("timeout_stage", nlohmann::json{}));
    for (const auto &choice_json :
         stage_json.value("choices", nlohmann::json::array())) {
      MissionChoice choice;
      choice.id = json_string(choice_json.value("id", nlohmann::json{}));
      choice.next_stage =
          json_string(choice_json.value("next", nlohmann::json{}));
      for (const auto &effect :
           choice_json.value("effects", nlohmann::json::array()))
        if (effect.is_string())
          choice.effects.push_back(effect.get<std::string>());
      if (choice.id.empty())
        return fail("choice in stage '" + stage_id + "' requires an 'id'");
      stage.choices.push_back(std::move(choice));
    }
    definition.stages.emplace(stage_id, std::move(stage));
  }
  // Referential integrity: every referenced stage must exist.
  for (const auto &trigger : definition.triggers)
    if (!definition.stages.contains(trigger.stage))
      return fail("trigger references unknown stage '" + trigger.stage + "'");
  for (const auto &[id, stage] : definition.stages) {
    if (!stage.timeout_stage.empty() &&
        !definition.stages.contains(stage.timeout_stage))
      return fail("stage '" + id + "' has unknown timeout_stage '" +
                  stage.timeout_stage + "'");
    for (const auto &choice : stage.choices)
      if (!choice.next_stage.empty() &&
          !definition.stages.contains(choice.next_stage))
        return fail("choice '" + choice.id + "' in stage '" + id +
                    "' targets unknown stage '" + choice.next_stage + "'");
  }
  return definition;
}

MissionRuntime::MissionRuntime(EventBus *bus) : bus_(bus) {}

bool MissionRuntime::add_definition(MissionDefinition definition,
                                    std::string *error) {
  if (definition.id.empty()) {
    if (error != nullptr)
      *error = "mission requires an 'id'";
    return false;
  }
  definitions_[definition.id] = std::move(definition);
  return true;
}

void MissionRuntime::clear() {
  definitions_.clear();
  instances_.clear();
}

bool MissionRuntime::conditions_match(const MissionTrigger &trigger,
                                      std::string_view payload) const {
  if (trigger.conditions.empty())
    return true;
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(payload);
  } catch (...) {
    return false;
  }
  for (const auto &condition : trigger.conditions) {
    const auto value = field_value(parsed, condition.field);
    if (!value || *value != condition.equals)
      return false;
  }
  return true;
}

void MissionRuntime::handle_event(std::string_view event_name,
                                  std::string_view payload) {
  // Collect matches first and instantiate in mission-id order so a single
  // event that triggers several missions behaves deterministically.
  std::vector<std::pair<std::string_view, const MissionTrigger *>> matched;
  for (const auto &[id, definition] : definitions_)
    for (const auto &trigger : definition.triggers)
      if (trigger.event == event_name &&
          conditions_match(trigger, payload)) {
        matched.emplace_back(id, &trigger);
        break; // one trigger per event per mission
      }
  std::sort(matched.begin(), matched.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  for (const auto &[id, trigger] : matched) {
    MissionInstance instance;
    instance.id = next_instance_id_++;
    instance.mission_id = id;
    enter_stage(instance, trigger->stage);
    instances_.push_back(std::move(instance));
  }
}

void MissionRuntime::enter_stage(MissionInstance &instance,
                                 const std::string &stage_id) {
  instance.stage_id = stage_id;
  const auto &definition = definitions_.at(instance.mission_id);
  const auto found = definition.stages.find(stage_id);
  if (found == definition.stages.end())
    return;
  instance.days_remaining = found->second.timer_days;
  if (bus_ != nullptr)
    bus_->publish(MissionEffectEvent{instance.mission_id, instance.id,
                                     stage_id, {}});
}

void MissionRuntime::advance(double elapsed_days) {
  if (elapsed_days <= 0)
    return;
  for (auto &instance : instances_) {
    const auto &definition = definitions_.at(instance.mission_id);
    const auto stage = definition.stages.find(instance.stage_id);
    if (stage == definition.stages.end() || stage->second.timer_days <= 0)
      continue;
    instance.days_remaining -= elapsed_days;
    if (instance.days_remaining <= 0 &&
        !stage->second.timeout_stage.empty()) {
      if (bus_ != nullptr)
        bus_->publish(MissionEffectEvent{instance.mission_id, instance.id,
                                         stage->second.timeout_stage,
                                         {"timeout:" + stage->first}});
      enter_stage(instance, stage->second.timeout_stage);
    }
  }
}

bool MissionRuntime::choose(std::uint64_t instance_id,
                            std::string_view choice_id) {
  const auto found = std::find_if(
      instances_.begin(), instances_.end(),
      [&](const auto &instance) { return instance.id == instance_id; });
  if (found == instances_.end())
    return false;
  const auto &definition = definitions_.at(found->mission_id);
  const auto stage = definition.stages.find(found->stage_id);
  if (stage == definition.stages.end())
    return false;
  for (const auto &choice : stage->second.choices) {
    if (choice.id != choice_id)
      continue;
    if (bus_ != nullptr)
      bus_->publish(MissionEffectEvent{found->mission_id, found->id,
                                       choice.next_stage, choice.effects});
    if (choice.next_stage.empty())
      instances_.erase(found); // terminal choice completes the mission
    else
      enter_stage(*found, choice.next_stage);
    return true;
  }
  return false;
}

std::vector<MissionInstance> MissionRuntime::instances() const {
  return instances_;
}

const MissionDefinition *
MissionRuntime::definition(std::string_view id) const {
  const auto found = definitions_.find(std::string(id));
  return found == definitions_.end() ? nullptr : &found->second;
}

std::string MissionRuntime::serialize() const {
  nlohmann::json doc = nlohmann::json::object();
  doc["next_instance_id"] = next_instance_id_;
  doc["instances"] = nlohmann::json::array();
  for (const auto &instance : instances_)
    doc["instances"].push_back({{"id", instance.id},
                                {"mission", instance.mission_id},
                                {"stage", instance.stage_id},
                                {"days_remaining", instance.days_remaining}});
  return doc.dump();
}

bool MissionRuntime::restore(std::string_view document, std::string *error) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(document);
  } catch (const std::exception &ex) {
    if (error != nullptr)
      *error = ex.what();
    return false;
  }
  if (!doc.is_object()) {
    if (error != nullptr)
      *error = "mission runtime snapshot must be an object";
    return false;
  }
  std::vector<MissionInstance> restored;
  std::uint64_t next_id = doc.value("next_instance_id", std::uint64_t{1});
  for (const auto &entry : doc.value("instances", nlohmann::json::array())) {
    MissionInstance instance;
    instance.id = entry.value("id", std::uint64_t{});
    instance.mission_id = json_string(entry.value("mission", nlohmann::json{}));
    instance.stage_id = json_string(entry.value("stage", nlohmann::json{}));
    instance.days_remaining = entry.value("days_remaining", 0.0);
    if (!definitions_.contains(instance.mission_id) ||
        !definitions_.at(instance.mission_id)
             .stages.contains(instance.stage_id)) {
      if (error != nullptr)
        *error = "snapshot references unknown mission/stage for instance " +
                 std::to_string(instance.id);
      return false;
    }
    next_id = std::max(next_id, instance.id + 1);
    restored.push_back(std::move(instance));
  }
  instances_ = std::move(restored);
  next_instance_id_ = next_id;
  return true;
}

} // namespace stellar::engine
