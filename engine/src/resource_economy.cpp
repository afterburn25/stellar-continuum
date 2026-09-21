#include <stellar/engine/resource_economy.hpp>

#include <algorithm>
#include <utility>

namespace stellar::engine {

double Inventory::quantity(std::string_view resource) const {
  const auto found = quantities_.find(std::string(resource));
  return found == quantities_.end() ? 0.0 : found->second;
}

double Inventory::add(std::string_view resource, double amount) {
  if (amount <= 0)
    return 0.0;
  const double accepted = std::min(amount, free_space());
  if (accepted <= 0)
    return 0.0;
  quantities_[std::string(resource)] += accepted;
  stored_total_ += accepted;
  return accepted;
}

double Inventory::remove(std::string_view resource, double amount) {
  if (amount <= 0)
    return 0.0;
  const auto found = quantities_.find(std::string(resource));
  if (found == quantities_.end())
    return 0.0;
  const double taken = std::min(amount, found->second);
  found->second -= taken;
  stored_total_ -= taken;
  if (found->second <= 0)
    quantities_.erase(found);
  return taken;
}

bool Inventory::contains(std::string_view resource, double amount) const {
  return quantity(resource) >= amount;
}

std::unordered_map<std::string, double> Inventory::snapshot() const {
  return quantities_;
}

void ResourceNetwork::define(ResourceDefinition definition) {
  definitions_[definition.id] = std::move(definition);
}
const ResourceDefinition *
ResourceNetwork::definition(std::string_view id) const {
  const auto found = definitions_.find(std::string(id));
  return found == definitions_.end() ? nullptr : &found->second;
}

EconomyNode &ResourceNetwork::add_node(std::uint64_t id, double capacity) {
  if (auto *existing = node(id))
    return *existing;
  nodes_.push_back(EconomyNode{id, Inventory(capacity)});
  return nodes_.back();
}

EconomyNode *ResourceNetwork::node(std::uint64_t id) {
  for (auto &candidate : nodes_)
    if (candidate.id == id)
      return &candidate;
  return nullptr;
}
const EconomyNode *ResourceNetwork::node(std::uint64_t id) const {
  for (const auto &candidate : nodes_)
    if (candidate.id == id)
      return &candidate;
  return nullptr;
}

std::uint64_t ResourceNetwork::add_producer(std::uint64_t node_id,
                                            std::string recipe_id) {
  producers_.push_back(
      ProducerBinding{Producer{next_producer_id_, std::move(recipe_id)},
                      node_id});
  return next_producer_id_++;
}

bool ResourceNetwork::set_producer_enabled(std::uint64_t producer_id,
                                           bool enabled) {
  for (auto &binding : producers_)
    if (binding.producer.id == producer_id) {
      binding.producer.enabled = enabled;
      return true;
    }
  return false;
}

void ResourceNetwork::add_recipe(Recipe recipe) {
  recipes_[recipe.id] = std::move(recipe);
}
const Recipe *ResourceNetwork::recipe(std::string_view id) const {
  const auto found = recipes_.find(std::string(id));
  return found == recipes_.end() ? nullptr : &found->second;
}

std::uint64_t ResourceNetwork::transfer(std::uint64_t from, std::uint64_t to,
                                        std::string resource, double amount,
                                        double rate_per_day) {
  transfers_.push_back(TransferOrder{next_order_id_, std::move(resource),
                                     amount, 0.0, from, to,
                                     std::max(0.0, rate_per_day)});
  return next_order_id_++;
}

bool ResourceNetwork::cancel_transfer(std::uint64_t order_id) {
  const auto found =
      std::find_if(transfers_.begin(), transfers_.end(),
                   [&](const auto &order) { return order.id == order_id; });
  if (found == transfers_.end())
    return false;
  transfers_.erase(found);
  return true;
}

void ResourceNetwork::advance(double elapsed_days) {
  if (elapsed_days <= 0)
    return;
  shortages_.clear();

  // Producers: progress accrues; a completed run consumes inputs then emits
  // outputs. Inputs are validated before consumption — a short input stalls
  // the producer at its current progress and reports a shortage.
  for (auto &binding : producers_) {
    auto &producer = binding.producer;
    if (!producer.enabled)
      continue;
    const auto recipe = recipes_.find(producer.recipe_id);
    if (recipe == recipes_.end())
      continue;
    auto *target = node(binding.node_id);
    if (target == nullptr)
      continue;
    producer.progress_days += elapsed_days;
    while (producer.progress_days >= recipe->second.duration_days) {
      bool affordable = true;
      for (const auto &[resource, amount] : recipe->second.inputs)
        if (!target->inventory.contains(resource, amount)) {
          affordable = false;
          shortages_.push_back(Shortage{producer.id, resource, amount,
                                        target->inventory.quantity(resource)});
        }
      if (!affordable) {
        producer.progress_days = recipe->second.duration_days;
        break;
      }
      for (const auto &[resource, amount] : recipe->second.inputs)
        target->inventory.remove(resource, amount);
      for (const auto &[resource, amount] : recipe->second.outputs)
        target->inventory.add(resource, amount);
      producer.progress_days -= recipe->second.duration_days;
    }
  }

  // Transfers move bounded amounts per tick; the source shortage caps the
  // shipment.
  for (auto it = transfers_.begin(); it != transfers_.end();) {
    auto &order = *it;
    auto *from = node(order.from_node);
    auto *to = node(order.to_node);
    bool erase = false;
    if (from == nullptr || to == nullptr || order.rate_per_day <= 0) {
      erase = true;
    } else {
      const double step = std::min(order.amount - order.shipped,
                                   order.rate_per_day * elapsed_days);
      const double taken = from->inventory.remove(order.resource, step);
      const double stored = to->inventory.add(order.resource, taken);
      // Anything the destination cannot hold returns to the source.
      if (stored < taken)
        from->inventory.add(order.resource, taken - stored);
      order.shipped += stored;
      if (order.complete())
        erase = true;
      else if (taken <= 0)
        shortages_.push_back(Shortage{0, order.resource, step, 0.0});
    }
    if (erase)
      it = transfers_.erase(it);
    else
      ++it;
  }
}

std::vector<TransferOrder> ResourceNetwork::transfers() const {
  return transfers_;
}

} // namespace stellar::engine
