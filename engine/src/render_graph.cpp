#include <stellar/engine/render_graph.hpp>

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <utility>

namespace stellar::engine {

ResourceId RenderGraph::add_resource(RenderResourceDesc desc) {
  resources_.push_back(std::move(desc));
  return static_cast<ResourceId>(resources_.size() - 1);
}

const RenderResourceDesc *RenderGraph::resource(ResourceId id) const {
  return id < resources_.size() ? &resources_[id] : nullptr;
}

ResourceId RenderGraph::find_resource(std::string_view name) const {
  for (std::size_t i = 0; i < resources_.size(); ++i)
    if (resources_[i].name == name)
      return static_cast<ResourceId>(i);
  return invalid_resource;
}

void RenderGraph::add_pass(RenderPass pass) {
  passes_.push_back(std::move(pass));
}
RenderPass *RenderGraph::pass(std::string_view name) {
  for (auto &candidate : passes_)
    if (candidate.name == name)
      return &candidate;
  return nullptr;
}
void RenderGraph::clear() {
  resources_.clear();
  passes_.clear();
}

bool RenderGraph::compile(
    std::vector<std::string> *execution_order,
    std::vector<RenderGraphDiagnostic> *diagnostics) {
  auto error = [&](std::string message) {
    if (diagnostics != nullptr)
      diagnostics->push_back(
          {RenderGraphDiagnostic::Severity::Error, std::move(message)});
  };
  bool ok = true;

  std::unordered_map<std::string, std::size_t> pass_index;
  for (std::size_t i = 0; i < passes_.size(); ++i) {
    if (passes_[i].name.empty()) {
      error("pass at index " + std::to_string(i) + " has no name");
      ok = false;
      continue;
    }
    if (!pass_index.emplace(passes_[i].name, i).second) {
      error("duplicate pass name '" + passes_[i].name + "'");
      ok = false;
    }
  }
  if (!ok)
    return false;

  // Resource reference validation + single-writer rule.
  for (const auto &p : passes_) {
    if (!p.enabled)
      continue;
    for (const auto id : p.reads)
      if (resource(id) == nullptr) {
        error("pass '" + p.name + "' reads unknown resource " +
              std::to_string(id));
        ok = false;
      }
    for (const auto id : p.writes)
      if (resource(id) == nullptr) {
        error("pass '" + p.name + "' writes unknown resource " +
              std::to_string(id));
        ok = false;
      }
  }
  std::unordered_map<ResourceId, std::string> writers;
  for (const auto &p : passes_) {
    if (!p.enabled)
      continue;
    for (const auto id : p.writes) {
      const auto [_, inserted] = writers.emplace(id, p.name);
      if (!inserted) {
        error("resource '" + resources_[id].name +
              "' is written by both '" + writers.at(id) + "' and '" +
              p.name + "'");
        ok = false;
      }
    }
  }
  if (!ok)
    return false;

  // Edges: writer -> reader for each resource, plus explicit ordering
  // constraints. Kahn's algorithm with declaration-order tie-breaking.
  const std::size_t n = passes_.size();
  std::vector<std::vector<std::size_t>> edges(n);
  std::vector<std::size_t> indegree(n, 0);
  auto add_edge = [&](std::size_t from, std::size_t to) {
    if (from == to)
      return;
    auto &list = edges[from];
    if (std::find(list.begin(), list.end(), to) == list.end()) {
      list.push_back(to);
      ++indegree[to];
    }
  };
  for (std::size_t i = 0; i < n; ++i) {
    if (!passes_[i].enabled)
      continue;
    for (const auto id : passes_[i].reads)
      if (const auto writer = writers.find(id); writer != writers.end())
        add_edge(pass_index.at(writer->second), i);
    for (const auto &after : passes_[i].ordering_constraints) {
      const auto found = pass_index.find(after);
      if (found == pass_index.end()) {
        error("pass '" + passes_[i].name +
              "' orders after unknown pass '" + after + "'");
        ok = false;
        continue;
      }
      add_edge(found->second, i);
    }
  }
  if (!ok)
    return false;

  std::deque<std::size_t> ready;
  for (std::size_t i = 0; i < n; ++i)
    if (passes_[i].enabled && indegree[i] == 0)
      ready.push_back(i);
  std::vector<std::string> order;
  while (!ready.empty()) {
    // Pop the lowest declaration index for deterministic ordering.
    const auto it = std::min_element(ready.begin(), ready.end());
    const std::size_t current = *it;
    ready.erase(it);
    order.push_back(passes_[current].name);
    for (const auto next : edges[current])
      if (--indegree[next] == 0)
        ready.push_back(next);
  }
  if (order.size() < static_cast<std::size_t>(std::count_if(
                         passes_.begin(), passes_.end(),
                         [](const auto &p) { return p.enabled; }))) {
    error("render graph contains a dependency cycle");
    return false;
  }
  if (execution_order != nullptr)
    *execution_order = std::move(order);
  return true;
}

std::vector<std::string> RenderGraph::producers(ResourceId id) const {
  std::vector<std::string> result;
  for (const auto &p : passes_)
    if (std::find(p.writes.begin(), p.writes.end(), id) != p.writes.end())
      result.push_back(p.name);
  return result;
}
std::vector<std::string> RenderGraph::consumers(ResourceId id) const {
  std::vector<std::string> result;
  for (const auto &p : passes_)
    if (std::find(p.reads.begin(), p.reads.end(), id) != p.reads.end())
      result.push_back(p.name);
  return result;
}

} // namespace stellar::engine
