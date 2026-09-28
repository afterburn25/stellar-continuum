#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Render graph: passes declare the transient/persistent resources they read
// and write; the graph validates the DAG and emits a deterministic execution
// order plus per-resource producer/consumer metadata the backend uses for
// barriers and transient-target reuse. The graph itself is backend-agnostic —
// it never touches Vulkan objects.

using ResourceId = std::uint32_t;
inline constexpr ResourceId invalid_resource = ~ResourceId{0};

struct RenderResourceDesc {
  enum class Kind { Texture2D, Buffer, Attachment };
  Kind kind{Kind::Texture2D};
  std::string name;
  std::uint32_t width{}, height{}; // 0 = frame-relative (swapchain size)
  std::string format;              // backend format token, e.g. "rgba16f"
  bool transient{true};            // transient = reused within the frame
};

struct RenderPass {
  std::string name;
  std::vector<ResourceId> reads;
  std::vector<ResourceId> writes;
  // Ordered after these passes even without a data dependency.
  std::vector<std::string> ordering_constraints;
  // Execution callback key — the backend binds an actual draw routine to
  // this string at compile time.
  std::string execute_tag;
  bool enabled{true};
};

struct RenderGraphDiagnostic {
  enum class Severity { Error, Warning } severity;
  std::string message;
};

class RenderGraph {
public:
  ResourceId add_resource(RenderResourceDesc desc);
  const RenderResourceDesc *resource(ResourceId id) const;
  ResourceId find_resource(std::string_view name) const;

  void add_pass(RenderPass pass);
  RenderPass *pass(std::string_view name);
  void clear();

  // Validates the graph and computes execution order:
  //  - every read/written resource exists
  //  - a resource written by multiple enabled passes is an error
  //  - dependency + ordering edges must be acyclic
  // Order is deterministic: topological, ties broken by declaration order.
  bool compile(std::vector<std::string> *execution_order,
               std::vector<RenderGraphDiagnostic> *diagnostics = nullptr);

  // Passes that write `id` (should be 0 or 1 after a successful compile).
  std::vector<std::string> producers(ResourceId id) const;
  std::vector<std::string> consumers(ResourceId id) const;

  std::size_t pass_count() const noexcept { return passes_.size(); }
  std::size_t resource_count() const noexcept { return resources_.size(); }

private:
  std::vector<RenderResourceDesc> resources_;
  std::vector<RenderPass> passes_;
};

} // namespace stellar::engine
