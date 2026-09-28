#include <stellar/engine/texture_streaming.hpp>

#include <algorithm>
#include <numeric>
#include <utility>

namespace stellar::engine {

TextureId TextureStreamer::register_texture(TextureDesc desc) {
  const auto existing = by_name_.find(desc.name);
  if (existing != by_name_.end())
    return existing->second;
  const auto id = static_cast<TextureId>(textures_.size());
  by_name_.emplace(desc.name, id);
  textures_.push_back(TextureState{std::move(desc)});
  return id;
}

const TextureDesc *TextureStreamer::texture(TextureId id) const {
  return id < textures_.size() ? &textures_[id].desc : nullptr;
}

TextureId TextureStreamer::find(std::string_view name) const {
  const auto found = by_name_.find(std::string(name));
  return found == by_name_.end() ? invalid_texture : found->second;
}

void TextureStreamer::request(TextureId id, std::uint32_t desired_mip,
                              float priority) {
  if (id >= textures_.size())
    return;
  auto &state = textures_[id];
  state.last_priority = priority;
  state.requested_mip = desired_mip;
  state.requested = true;
  // last_requested_frame is stamped in advance_frame.
}

void TextureStreamer::set_pinned(TextureId id, bool pinned) {
  if (id < textures_.size())
    textures_[id].pinned = pinned;
}

std::uint64_t TextureStreamer::mip_bytes(const TextureState &state,
                                         std::uint32_t mip) const {
  return mip < state.desc.mip_bytes.size() ? state.desc.mip_bytes[mip] : 0;
}

std::vector<ResidencyChange>
TextureStreamer::advance_frame(std::uint64_t frame_index) {
  std::vector<ResidencyChange> changes;

  // Stamp requests and compute desired residency (contiguous finest..end).
  struct Demand {
    TextureId id;
    std::uint32_t desired_mip;
    float priority;
    std::uint64_t bytes_needed;
  };
  std::vector<Demand> demands;
  for (std::size_t i = 0; i < textures_.size(); ++i) {
    auto &state = textures_[i];
    const auto mip_count =
        static_cast<std::uint32_t>(state.desc.mip_bytes.size());
    if (mip_count == 0)
      continue;
    // Clamp desired mip into range.
    state.requested_mip =
        std::min(state.requested_mip, mip_count - 1);
    const bool requested = state.requested;
    state.requested = false; // consumed; must be re-declared next frame
    if (!requested && !state.pinned)
      continue;
    state.last_requested_frame = frame_index;
    // Desired residency: keep mips [desired, mip_count) resident — a
    // contiguous tail so any mip between the finest and coarsest is usable.
    const auto target_finest =
        state.pinned ? std::uint32_t{0} : state.requested_mip;
    std::uint64_t bytes = 0;
    for (std::uint32_t m = target_finest; m < mip_count; ++m)
      bytes += mip_bytes(state, m);
    demands.push_back(Demand{static_cast<TextureId>(i), target_finest,
                             state.pinned ? std::numeric_limits<float>::max()
                                          : state.last_priority,
                             bytes});
  }

  // Admit demands in priority order until the budget is exhausted.
  std::sort(demands.begin(), demands.end(), [](const auto &a, const auto &b) {
    if (a.priority != b.priority)
      return a.priority > b.priority;
    return a.id < b.id;
  });
  std::uint64_t spent = 0;
  std::unordered_map<TextureId, std::uint32_t> admitted;
  for (const auto &demand : demands) {
    if (spent + demand.bytes_needed <= budget_) {
      spent += demand.bytes_needed;
      admitted.emplace(demand.id, demand.desired_mip);
    }
  }

  // Apply residency: shrink to admitted level or evict entirely; then grow.
  resident_bytes_ = 0;
  for (std::size_t i = 0; i < textures_.size(); ++i) {
    auto &state = textures_[i];
    const auto mip_count =
        static_cast<std::uint32_t>(state.desc.mip_bytes.size());
    const auto target = admitted.find(static_cast<TextureId>(i));
    if (target == admitted.end()) {
      // Evict everything.
      if (state.finest_resident)
        for (std::uint32_t m = *state.finest_resident; m < mip_count; ++m)
          changes.push_back(ResidencyChange{static_cast<TextureId>(i), m,
                                            false});
      state.finest_resident.reset();
      continue;
    }
    const auto target_finest = target->second;
    if (state.finest_resident) {
      // Evict mips finer than target (m < target_finest are resident extras
      // only when finest_resident < target_finest — shrink case).
      if (*state.finest_resident < target_finest)
        for (std::uint32_t m = *state.finest_resident; m < target_finest; ++m)
          changes.push_back(ResidencyChange{static_cast<TextureId>(i), m,
                                            false});
    }
    resident_bytes_ += [&] {
      std::uint64_t sum = 0;
      for (std::uint32_t m = target_finest; m < mip_count; ++m)
        sum += mip_bytes(state, m);
      return sum;
    }();
  }
  // Second pass: emit loads for newly admitted or promoted textures.
  for (const auto &[id, target_finest] : admitted) {
    auto &state = textures_[id];
    const auto mip_count =
        static_cast<std::uint32_t>(state.desc.mip_bytes.size());
    const std::uint32_t load_until =
        state.finest_resident ? *state.finest_resident : mip_count;
    for (std::uint32_t m = target_finest; m < load_until; ++m)
      changes.push_back(ResidencyChange{id, m, true});
    state.finest_resident = target_finest;
  }
  return changes;
}

std::optional<std::uint32_t>
TextureStreamer::finest_resident_mip(TextureId id) const {
  if (id >= textures_.size())
    return std::nullopt;
  return textures_[id].finest_resident;
}

std::uint32_t TextureStreamer::resident_mip_count(TextureId id) const {
  if (id >= textures_.size() || !textures_[id].finest_resident)
    return 0;
  const auto &state = textures_[id];
  return static_cast<std::uint32_t>(state.desc.mip_bytes.size()) -
         *state.finest_resident;
}

} // namespace stellar::engine
