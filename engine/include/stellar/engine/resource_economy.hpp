#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Generic resource/economy substrate. Concrete resources (food, metals,
// hydrogen, ...) are data — ResourceDefinition rows — not code. Inventories
// hold quantities; Recipes convert inputs to outputs over time; a
// ResourceNetwork advances producers and resolves transfer orders with
// deterministic per-tick accounting. Shortages are reported, never thrown.

struct ResourceDefinition {
  std::string id;          // stable content id, e.g. "res.metals"
  std::string name_key;    // localization key
  bool stockpiles{true};   // false = flow resource (power, capacity)
};

class Inventory {
public:
  explicit Inventory(double capacity = std::numeric_limits<double>::infinity())
      : capacity_(capacity) {}

  double quantity(std::string_view resource) const;
  double capacity() const noexcept { return capacity_; }
  double free_space() const noexcept {
    return capacity_ - stored_total_;
  }
  double stored_total() const noexcept { return stored_total_; }

  // Returns the amount actually stored (<= amount when capacity-limited).
  double add(std::string_view resource, double amount);
  // Returns the amount actually removed (<= amount when stock is short).
  double remove(std::string_view resource, double amount);
  bool contains(std::string_view resource, double amount) const;

  std::unordered_map<std::string, double> snapshot() const;

private:
  std::unordered_map<std::string, double> quantities_;
  double capacity_;
  double stored_total_{};
};

struct Recipe {
  std::string id;
  // resource id -> units consumed/produced per run.
  std::vector<std::pair<std::string, double>> inputs;
  std::vector<std::pair<std::string, double>> outputs;
  double duration_days{1.0};
};

struct Producer {
  std::uint64_t id{};
  std::string recipe_id;
  double progress_days{};      // accumulates toward recipe.duration_days
  bool enabled{true};
};

struct TransferOrder {
  std::uint64_t id{};
  std::string resource;
  double amount{};
  double shipped{};            // delivered so far
  std::uint64_t from_node{}, to_node{};
  double rate_per_day{};
  bool complete() const { return shipped >= amount; }
};

struct Shortage {
  std::uint64_t producer_id{};
  std::string resource;
  double required{}, available{};
};

// A node is anything with an inventory: colony, ship, station.
struct EconomyNode {
  std::uint64_t id{};
  Inventory inventory;
};

class ResourceNetwork {
public:
  void define(ResourceDefinition definition);
  const ResourceDefinition *definition(std::string_view id) const;
  void add_recipe(Recipe recipe);
  const Recipe *recipe(std::string_view id) const;

  EconomyNode &add_node(std::uint64_t id, double capacity =
                                             std::numeric_limits<double>::infinity());
  EconomyNode *node(std::uint64_t id);
  const EconomyNode *node(std::uint64_t id) const;

  std::uint64_t add_producer(std::uint64_t node_id, std::string recipe_id);
  bool set_producer_enabled(std::uint64_t producer_id, bool enabled);

  // Orders per-day transfer between nodes. Returns the order id.
  std::uint64_t transfer(std::uint64_t from, std::uint64_t to,
                         std::string resource, double amount,
                         double rate_per_day);
  bool cancel_transfer(std::uint64_t order_id);

  // Advances all enabled producers and transfer orders by elapsed days.
  // Deterministic: producers and orders process in registration order.
  void advance(double elapsed_days);

  std::vector<Shortage> shortages() const { return shortages_; }
  std::vector<TransferOrder> transfers() const;

  // --- persistence ---------------------------------------------------
  // Serializable network state: node inventories, producer progress and
  // open transfer orders, plus the id counters so post-load ids never
  // collide with restored ones. ResourceDefinition/Recipe rows are
  // definitions — re-registered on load.
  struct NodeState {
    std::uint64_t id{};
    double capacity{0.0};
    std::vector<std::pair<std::string, double>> resources; // sorted by id
  };
  struct ProducerState {
    std::uint64_t id{};
    std::uint64_t node_id{};
    std::string recipe_id;
    double progress_days{0.0};
    bool enabled{true};
  };
  struct State {
    std::uint32_t version{1};
    std::uint64_t next_producer_id{1};
    std::uint64_t next_order_id{1};
    std::vector<NodeState> nodes;            // sorted by id
    std::vector<ProducerState> producers;    // sorted by id
    std::vector<TransferOrder> transfers;    // sorted by id
  };
  [[nodiscard]] State capture_state() const;
  // Replaces runtime state with the snapshot. Throws invalid_argument on
  // a producer referencing an unknown recipe or node.
  void restore_state(const State& state);

private:
  std::unordered_map<std::string, ResourceDefinition> definitions_;
  // deque keeps node references stable as nodes are added.
  std::deque<EconomyNode> nodes_;
  std::unordered_map<std::string, Recipe> recipes_;
  struct ProducerBinding {
    Producer producer;
    std::uint64_t node_id{};
  };
  std::vector<ProducerBinding> producers_;
  std::vector<TransferOrder> transfers_;
  std::vector<Shortage> shortages_;
  std::uint64_t next_producer_id_{1};
  std::uint64_t next_order_id_{1};
};

} // namespace stellar::engine
