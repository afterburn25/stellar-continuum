#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Texture streaming/residency policy. The renderer registers textures with
// per-mip byte sizes; each frame it declares desired mip levels, and the
// streamer computes which mips should be resident under the VRAM budget,
// producing a load queue (async IO pulls) and an eviction list. Actual GPU
// upload is the backend's job — this class owns the *decision*, which is
// where budget discipline and determinism live.

using TextureId = std::uint32_t;
inline constexpr TextureId invalid_texture = ~TextureId{0};

struct TextureDesc {
  std::string name;
  // Bytes per mip level, index 0 = full resolution.
  std::vector<std::uint64_t> mip_bytes;
};

struct StreamingRequest {
  TextureId id{};
  // Highest mip the requester wants resident (0 = full res).
  std::uint32_t desired_mip{};
  float priority{1.0f}; // higher wins budget contention
};

struct ResidencyChange {
  TextureId id{};
  std::uint32_t mip{};    // mip level affected
  bool load{};            // true = needs loading, false = evict
};

class TextureStreamer {
public:
  explicit TextureStreamer(std::uint64_t vram_budget_bytes)
      : budget_(vram_budget_bytes) {}

  TextureId register_texture(TextureDesc desc);
  const TextureDesc *texture(TextureId id) const;
  TextureId find(std::string_view name) const;

  void set_budget(std::uint64_t bytes) { budget_ = bytes; }
  std::uint64_t budget() const noexcept { return budget_; }
  std::uint64_t resident_bytes() const noexcept { return resident_bytes_; }

  // Declare this frame's demand for `id` at `desired_mip`.
  void request(TextureId id, std::uint32_t desired_mip, float priority = 1.0f);
  // Pin a texture: it is never evicted (UI atlases, fallback textures).
  void set_pinned(TextureId id, bool pinned);

  // Recomputes residency under the budget. Returns the ordered change list:
  // evictions first (frees budget), then loads sorted by priority.
  std::vector<ResidencyChange> advance_frame(std::uint64_t frame_index);

  // Currently resident coarsest->finest mip range, or nullopt when unloaded.
  std::optional<std::uint32_t> finest_resident_mip(TextureId id) const;
  std::uint32_t resident_mip_count(TextureId id) const;

private:
  struct TextureState {
    TextureDesc desc;
    bool pinned{};
    float last_priority{};
    std::uint64_t last_requested_frame{};
    std::uint32_t requested_mip{};
    bool requested{};
    // Resident set: contiguous range [finest_resident, mip_count).
    std::optional<std::uint32_t> finest_resident;
  };

  std::uint64_t mip_bytes(const TextureState &state, std::uint32_t mip) const;

  std::vector<TextureState> textures_;
  std::unordered_map<std::string, TextureId> by_name_;
  std::uint64_t budget_{};
  std::uint64_t resident_bytes_{};
};

} // namespace stellar::engine
