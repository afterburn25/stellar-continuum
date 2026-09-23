#pragma once

// JSON codecs for the specialization frameworks' capture_state() structs.
//
// Mirrors the core-layer convention (galaxy_phenomena_json.hpp): templated
// to_json/from_json free functions so callers may use nlohmann::json or any
// value API exposing at()/value()/get_to()/object construction. Byte-level
// persistence of engine framework state lives here so campaign save codecs
// can embed framework state without hand-writing field lists.
//
// Rules kept consistent with the struct-level persistence contract:
// - Definitions (profiles, specs, recipes, ship classes, actions) are not
//   serialized; owners re-register them and restore runtime state.
// - version fields are emitted and checked on load (>kCurrent throws).
// - Required fields use at(); additive/forgiving fields use value().

#include <stellar/engine/simulation_scheduler.hpp>
#include <stellar/engine/simulation_executor.hpp>
#include <stellar/engine/population.hpp>
#include <stellar/engine/colony.hpp>
#include <stellar/engine/flow_network.hpp>
#include <stellar/engine/logistics.hpp>
#include <stellar/engine/warfare.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <stellar/engine/resource_economy.hpp>

#include <stdexcept>

namespace stellar::engine {

namespace detail {
inline void check_version(std::uint32_t version, std::uint32_t supported,
                          const char* what) {
  if (version == 0 || version > supported)
    throw std::invalid_argument(std::string("Unsupported ") + what +
                                " state version " + std::to_string(version));
}
} // namespace detail

// --- SimulationScheduler -------------------------------------------------

template<class Json> inline void to_json(Json& j, const SimulationTier& v) {
  j = static_cast<int>(v);
}
template<class Json> inline void from_json(const Json& j, SimulationTier& v) {
  const int raw = j.template get<int>();
  if (raw < 0 || raw >= static_cast<int>(SimulationTier::Count))
    throw std::invalid_argument("Invalid simulation tier");
  v = static_cast<SimulationTier>(raw);
}

template<class Json>
inline void to_json(Json& j, const SimulationScheduler::ItemState& s) {
  j = Json{{"key", s.key},
           {"tier", s.tier},
           {"last_run", s.last_run},
           {"has_last_run", s.has_last_run},
           {"dormant_since", s.dormant_since},
           {"has_dormant_since", s.has_dormant_since}};
}
template<class Json>
inline void from_json(const Json& j, SimulationScheduler::ItemState& s) {
  j.at("key").get_to(s.key);
  j.at("tier").get_to(s.tier);
  s.last_run = j.value("last_run", Tick(0));
  s.has_last_run = j.value("has_last_run", false);
  s.dormant_since = j.value("dormant_since", Tick(0));
  s.has_dormant_since = j.value("has_dormant_since", false);
}

template<class Json>
inline void to_json(Json& j, const SimulationScheduler::State& s) {
  j = Json{{"version", s.version}, {"tick", s.tick}, {"items", s.items}};
}
template<class Json>
inline void from_json(const Json& j, SimulationScheduler::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "SimulationScheduler");
  j.at("tick").get_to(s.tick);
  j.at("items").get_to(s.items);
}

// --- SimulationExecutor --------------------------------------------------

template<class Json>
inline void to_json(Json& j, const SimulationExecutor::State& s) {
  j = Json{{"version", s.version},
           {"scheduler", s.scheduler},
           {"dirty", s.dirty},
           {"wake", s.wake},
           {"paused", s.paused}};
}
template<class Json>
inline void from_json(const Json& j, SimulationExecutor::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "SimulationExecutor");
  j.at("scheduler").get_to(s.scheduler);
  s.dirty = j.value("dirty", std::vector<SimulationExecutor::Key>{});
  s.wake = j.value("wake", std::vector<SimulationExecutor::Key>{});
  s.paused = j.value("paused", false);
}

// --- Population ----------------------------------------------------------

template<class Json> inline void to_json(Json& j, const EducationLevel& v) {
  j = static_cast<int>(v);
}
template<class Json> inline void from_json(const Json& j, EducationLevel& v) {
  const int raw = j.template get<int>();
  if (raw < 0 || raw >= static_cast<int>(EducationLevel::Count))
    throw std::invalid_argument("Invalid education level");
  v = static_cast<EducationLevel>(raw);
}
template<class Json> inline void to_json(Json& j, const WealthBracket& v) {
  j = static_cast<int>(v);
}
template<class Json> inline void from_json(const Json& j, WealthBracket& v) {
  const int raw = j.template get<int>();
  if (raw < 0 || raw >= static_cast<int>(WealthBracket::Count))
    throw std::invalid_argument("Invalid wealth bracket");
  v = static_cast<WealthBracket>(raw);
}
template<class Json> inline void to_json(Json& j, const CohortKey& k) {
  j = Json{{"profile", k.profile},
           {"culture", k.culture},
           {"occupation", k.occupation},
           {"education", k.education},
           {"wealth", k.wealth}};
}
template<class Json> inline void from_json(const Json& j, CohortKey& k) {
  j.at("profile").get_to(k.profile);
  k.culture = j.value("culture", std::string{});
  k.occupation = j.value("occupation", std::string{});
  k.education = j.value("education", EducationLevel::Basic);
  k.wealth = j.value("wealth", WealthBracket::Standard);
}
template<class Json>
inline void to_json(Json& j, const Population::CohortState& s) {
  j = Json{{"key", s.key},
           {"size", s.size},
           {"age_distribution", s.age_distribution},
           {"health", s.health},
           {"happiness", s.happiness},
           {"morale", s.morale},
           {"housing_coverage", s.housing_coverage},
           {"employment_rate", s.employment_rate},
           {"environment_suitability", s.environment_suitability},
           {"political_tendency", s.political_tendency}};
}
template<class Json>
inline void from_json(const Json& j, Population::CohortState& s) {
  j.at("key").get_to(s.key);
  j.at("size").get_to(s.size);
  s.age_distribution = j.value(
      "age_distribution",
      std::array<double, kAgeBuckets>{});
  s.health = j.value("health", 0.0);
  s.happiness = j.value("happiness", 0.0);
  s.morale = j.value("morale", 0.0);
  s.housing_coverage = j.value("housing_coverage", 0.0);
  s.employment_rate = j.value("employment_rate", 0.0);
  s.environment_suitability = j.value("environment_suitability", 0.0);
  s.political_tendency = j.value("political_tendency", 0.0);
}
template<class Json>
inline void to_json(Json& j, const Population::State& s) {
  j = Json{{"version", s.version}, {"cohorts", s.cohorts}};
}
template<class Json>
inline void from_json(const Json& j, Population::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "Population");
  j.at("cohorts").get_to(s.cohorts);
}

// --- Colony --------------------------------------------------------------

template<class Json>
inline void to_json(Json& j, const Colony::DistrictState& s) {
  j = Json{{"id", s.id},
           {"spec_id", s.spec_id},
           {"construction_remaining", s.construction_remaining},
           {"complete", s.complete},
           {"enabled", s.enabled}};
}
template<class Json>
inline void from_json(const Json& j, Colony::DistrictState& s) {
  j.at("id").get_to(s.id);
  j.at("spec_id").get_to(s.spec_id);
  s.construction_remaining = j.value("construction_remaining", 0.0);
  s.complete = j.value("complete", false);
  s.enabled = j.value("enabled", true);
}
template<class Json>
inline void to_json(Json& j, const Colony::StructureState& s) {
  j = Json{{"id", s.id},
           {"spec_id", s.spec_id},
           {"district_id", s.district_id},
           {"construction_remaining", s.construction_remaining},
           {"complete", s.complete},
           {"enabled", s.enabled},
           {"condition", s.condition},
           {"operating", s.operating}};
}
template<class Json>
inline void from_json(const Json& j, Colony::StructureState& s) {
  j.at("id").get_to(s.id);
  j.at("spec_id").get_to(s.spec_id);
  s.district_id = j.value("district_id", std::uint64_t{0});
  s.construction_remaining = j.value("construction_remaining", 0.0);
  s.complete = j.value("complete", false);
  s.enabled = j.value("enabled", true);
  s.condition = j.value("condition", 1.0);
  s.operating = j.value("operating", 1.0);
}
template<class Json>
inline void to_json(Json& j, const Colony::State& s) {
  j = Json{{"version", s.version},
           {"standalone_slots", s.standalone_slots},
           {"districts", s.districts},
           {"structures", s.structures}};
}
template<class Json>
inline void from_json(const Json& j, Colony::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "Colony");
  s.standalone_slots = j.value("standalone_slots", std::uint32_t{0});
  j.at("districts").get_to(s.districts);
  j.at("structures").get_to(s.structures);
}

// --- FlowNetwork ---------------------------------------------------------

template<class Json>
inline void to_json(Json& j, const FlowNetwork::NodeState& s) {
  j = Json{{"id", s.id},
           {"supply_per_day", s.supply_per_day},
           {"demand_per_day", s.demand_per_day},
           {"storage_capacity", s.storage_capacity},
           {"storage", s.storage},
           {"enabled", s.enabled}};
}
template<class Json>
inline void from_json(const Json& j, FlowNetwork::NodeState& s) {
  j.at("id").get_to(s.id);
  s.supply_per_day = j.value("supply_per_day", 0.0);
  s.demand_per_day = j.value("demand_per_day", 0.0);
  s.storage_capacity = j.value("storage_capacity", 0.0);
  s.storage = j.value("storage", 0.0);
  s.enabled = j.value("enabled", true);
}
template<class Json>
inline void to_json(Json& j, const FlowNetwork::EdgeState& s) {
  j = Json{{"id", s.id},
           {"from", s.from},
           {"to", s.to},
           {"capacity_per_day", s.capacity_per_day},
           {"enabled", s.enabled}};
}
template<class Json>
inline void from_json(const Json& j, FlowNetwork::EdgeState& s) {
  j.at("id").get_to(s.id);
  j.at("from").get_to(s.from);
  j.at("to").get_to(s.to);
  s.capacity_per_day = j.value("capacity_per_day", 0.0);
  s.enabled = j.value("enabled", true);
}
template<class Json>
inline void to_json(Json& j, const FlowNetwork::State& s) {
  j = Json{{"version", s.version},
           {"resource", s.resource},
           {"nodes", s.nodes},
           {"edges", s.edges}};
}
template<class Json>
inline void from_json(const Json& j, FlowNetwork::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "FlowNetwork");
  j.at("resource").get_to(s.resource);
  j.at("nodes").get_to(s.nodes);
  j.at("edges").get_to(s.edges);
}

// --- LogisticsNetwork ----------------------------------------------------

template<class Json>
inline void to_json(Json& j, const LogisticsNetwork::RouteState& s) {
  j = Json{{"id", s.id},
           {"path", s.path},
           {"leg_days", s.leg_days},
           {"capacity", s.capacity},
           {"enabled", s.enabled},
           {"in_flight", s.in_flight}};
}
template<class Json>
inline void from_json(const Json& j, LogisticsNetwork::RouteState& s) {
  j.at("id").get_to(s.id);
  j.at("path").get_to(s.path);
  j.at("leg_days").get_to(s.leg_days);
  s.capacity = j.value("capacity", 0.0);
  s.enabled = j.value("enabled", true);
  s.in_flight = j.value("in_flight", 0.0);
}
template<class Json>
inline void to_json(Json& j, const LogisticsNetwork::ShipmentState& s) {
  j = Json{{"id", s.id},
           {"route", s.route},
           {"resource", s.resource},
           {"quantity", s.quantity},
           {"departed", s.departed},
           {"eta", s.eta}};
}
template<class Json>
inline void from_json(const Json& j, LogisticsNetwork::ShipmentState& s) {
  j.at("id").get_to(s.id);
  j.at("route").get_to(s.route);
  j.at("resource").get_to(s.resource);
  s.quantity = j.value("quantity", 0.0);
  s.departed = j.value("departed", 0.0);
  s.eta = j.value("eta", 0.0);
}
template<class Json>
inline void to_json(Json& j, const LogisticsNetwork::State& s) {
  j = Json{{"version", s.version},
           {"now", s.now},
           {"nodes", s.nodes},
           {"routes", s.routes},
           {"queued", s.queued},
           {"in_transit", s.in_transit}};
}
template<class Json>
inline void from_json(const Json& j, LogisticsNetwork::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "LogisticsNetwork");
  s.now = j.value("now", 0.0);
  j.at("nodes").get_to(s.nodes);
  j.at("routes").get_to(s.routes);
  j.at("queued").get_to(s.queued);
  j.at("in_transit").get_to(s.in_transit);
}

// --- WarfareModel --------------------------------------------------------

template<class Json> inline void to_json(Json& j, const FleetOrderKind& v) {
  j = static_cast<int>(v);
}
template<class Json>
inline void from_json(const Json& j, FleetOrderKind& v) {
  const int raw = j.template get<int>();
  if (raw < 0 || raw > static_cast<int>(FleetOrderKind::Retreat))
    throw std::invalid_argument("Invalid fleet order kind");
  v = static_cast<FleetOrderKind>(raw);
}
template<class Json> inline void to_json(Json& j, const FleetOrder& s) {
  j = Json{{"kind", s.kind}, {"target_x", s.target_x}, {"target_y", s.target_y}};
}
template<class Json> inline void from_json(const Json& j, FleetOrder& s) {
  j.at("kind").get_to(s.kind);
  s.target_x = j.value("target_x", 0.0);
  s.target_y = j.value("target_y", 0.0);
}
template<class Json> inline void to_json(Json& j, const ShipCohort& s) {
  j = Json{{"ship_class", s.ship_class},
           {"count", s.count},
           {"condition", s.condition},
           {"experience", s.experience}};
}
template<class Json> inline void from_json(const Json& j, ShipCohort& s) {
  j.at("ship_class").get_to(s.ship_class);
  j.at("count").get_to(s.count);
  s.condition = j.value("condition", 1.0);
  s.experience = j.value("experience", 0.0);
}
template<class Json> inline void to_json(Json& j, const FleetState& s) {
  j = Json{{"id", s.id},
           {"owner", s.owner},
           {"x", s.x},
           {"y", s.y},
           {"order", s.order},
           {"engaged", s.engaged}};
}
template<class Json> inline void from_json(const Json& j, FleetState& s) {
  j.at("id").get_to(s.id);
  s.owner = j.value("owner", std::uint64_t{0});
  s.x = j.value("x", 0.0);
  s.y = j.value("y", 0.0);
  s.order = j.value("order", FleetOrder{});
  s.engaged = j.value("engaged", false);
}
template<class Json>
inline void to_json(Json& j, const WarfareModel::FleetEntry& s) {
  j = Json{{"state", s.state}, {"cohorts", s.cohorts}};
}
template<class Json>
inline void from_json(const Json& j, WarfareModel::FleetEntry& s) {
  j.at("state").get_to(s.state);
  j.at("cohorts").get_to(s.cohorts);
}
template<class Json>
inline void to_json(Json& j, const WarfareModel::State& s) {
  j = Json{{"version", s.version}, {"fleets", s.fleets}};
}
template<class Json>
inline void from_json(const Json& j, WarfareModel::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "WarfareModel");
  j.at("fleets").get_to(s.fleets);
}

// --- StrategicMind -------------------------------------------------------

template<class Json> inline void to_json(Json& j, const Decision& s) {
  j = Json{{"at_day", s.at_day},
           {"domain", s.domain},
           {"action_id", s.action_id},
           {"utility", s.utility},
           {"candidates", s.candidates},
           {"switched", s.switched}};
}
template<class Json> inline void from_json(const Json& j, Decision& s) {
  s.at_day = j.value("at_day", 0.0);
  j.at("domain").get_to(s.domain);
  j.at("action_id").get_to(s.action_id);
  s.utility = j.value("utility", 0.0);
  s.candidates = j.value("candidates", std::uint32_t{0});
  s.switched = j.value("switched", false);
}
template<class Json>
inline void to_json(Json& j, const StrategicMind::DomainIncumbent& s) {
  j = Json{{"domain", s.domain}, {"action_id", s.action_id}};
}
template<class Json>
inline void from_json(const Json& j, StrategicMind::DomainIncumbent& s) {
  j.at("domain").get_to(s.domain);
  j.at("action_id").get_to(s.action_id);
}
template<class Json>
inline void to_json(Json& j, const StrategicMind::CooldownStamp& s) {
  j = Json{{"action_id", s.action_id}, {"last_day", s.last_day}};
}
template<class Json>
inline void from_json(const Json& j, StrategicMind::CooldownStamp& s) {
  j.at("action_id").get_to(s.action_id);
  s.last_day = j.value("last_day", 0.0);
}
template<class Json>
inline void to_json(Json& j, const StrategicMind::State& s) {
  j = Json{{"version", s.version},
           {"incumbents", s.incumbents},
           {"last_commits", s.last_commits},
           {"journal", s.journal}};
}
template<class Json>
inline void from_json(const Json& j, StrategicMind::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "StrategicMind");
  s.incumbents =
      j.value("incumbents", std::vector<StrategicMind::DomainIncumbent>{});
  s.last_commits =
      j.value("last_commits", std::vector<StrategicMind::CooldownStamp>{});
  s.journal = j.value("journal", std::deque<Decision>{});
}

// --- ResourceNetwork -----------------------------------------------------

template<class Json> inline void to_json(Json& j, const TransferOrder& s) {
  j = Json{{"id", s.id},
           {"resource", s.resource},
           {"amount", s.amount},
           {"shipped", s.shipped},
           {"from_node", s.from_node},
           {"to_node", s.to_node},
           {"rate_per_day", s.rate_per_day}};
}
template<class Json> inline void from_json(const Json& j, TransferOrder& s) {
  j.at("id").get_to(s.id);
  j.at("resource").get_to(s.resource);
  s.amount = j.value("amount", 0.0);
  s.shipped = j.value("shipped", 0.0);
  s.from_node = j.value("from_node", std::uint64_t{0});
  s.to_node = j.value("to_node", std::uint64_t{0});
  s.rate_per_day = j.value("rate_per_day", 0.0);
}
template<class Json>
inline void to_json(Json& j, const ResourceNetwork::NodeState& s) {
  j = Json{{"id", s.id}, {"capacity", s.capacity}, {"resources", s.resources}};
}
template<class Json>
inline void from_json(const Json& j, ResourceNetwork::NodeState& s) {
  j.at("id").get_to(s.id);
  s.capacity = j.value("capacity", 0.0);
  j.at("resources").get_to(s.resources);
}
template<class Json>
inline void to_json(Json& j, const ResourceNetwork::ProducerState& s) {
  j = Json{{"id", s.id},
           {"node_id", s.node_id},
           {"recipe_id", s.recipe_id},
           {"progress_days", s.progress_days},
           {"enabled", s.enabled}};
}
template<class Json>
inline void from_json(const Json& j, ResourceNetwork::ProducerState& s) {
  j.at("id").get_to(s.id);
  j.at("node_id").get_to(s.node_id);
  j.at("recipe_id").get_to(s.recipe_id);
  s.progress_days = j.value("progress_days", 0.0);
  s.enabled = j.value("enabled", true);
}
template<class Json>
inline void to_json(Json& j, const ResourceNetwork::State& s) {
  j = Json{{"version", s.version},
           {"next_producer_id", s.next_producer_id},
           {"next_order_id", s.next_order_id},
           {"nodes", s.nodes},
           {"producers", s.producers},
           {"transfers", s.transfers}};
}
template<class Json>
inline void from_json(const Json& j, ResourceNetwork::State& s) {
  s.version = j.value("version", std::uint32_t{1});
  detail::check_version(s.version, 1, "ResourceNetwork");
  s.next_producer_id = j.value("next_producer_id", std::uint64_t{1});
  s.next_order_id = j.value("next_order_id", std::uint64_t{1});
  j.at("nodes").get_to(s.nodes);
  j.at("producers").get_to(s.producers);
  j.at("transfers").get_to(s.transfers);
}

} // namespace stellar::engine
