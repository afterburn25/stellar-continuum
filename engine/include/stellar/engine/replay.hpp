#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Deterministic replay/debugging support. A ReplayRecorder captures the
// ordered command stream (the only nondeterminism input to a fixed-step
// simulation), plus periodic RNG/state checkpoints. A ReplayPlayer streams
// the same commands back; checkpoints let a run verify it has not diverged.
//
// Commands are opaque (name + payload string) — the game decides what is a
// replayable command (orders, speed changes, seeds). Checkpoint hashes are
// FNV-1a over caller-provided state bytes; the recorder never inspects them.

struct ReplayCommand {
  std::uint64_t tick{};
  std::string name;
  std::string payload;
};

struct ReplayCheckpoint {
  std::uint64_t tick{};
  std::uint64_t hash{};
  std::string label;
};

struct ReplayHeader {
  std::uint64_t seed{};
  std::string build_id;
  std::string game_version;
};

std::uint64_t fnv1a64(std::string_view bytes) noexcept;

class ReplayRecorder {
public:
  explicit ReplayRecorder(ReplayHeader header = {});

  void record(std::uint64_t tick, std::string name, std::string payload);
  void checkpoint(std::uint64_t tick, std::uint64_t hash,
                  std::string label = {});

  const ReplayHeader &header() const { return header_; }
  const std::vector<ReplayCommand> &commands() const { return commands_; }
  const std::vector<ReplayCheckpoint> &checkpoints() const {
    return checkpoints_;
  }

  // Container-storage footprint for MemoryTracker::report — command
  // payloads grow unboundedly over a recording session, so occupancy is
  // worth tracking.
  [[nodiscard]] std::size_t estimated_memory_bytes() const noexcept {
    std::size_t total = commands_.capacity() * sizeof(ReplayCommand) +
                        checkpoints_.capacity() * sizeof(ReplayCheckpoint) +
                        header_.build_id.capacity() +
                        header_.game_version.capacity();
    for (const auto &command : commands_)
      total += command.name.capacity() + command.payload.capacity();
    for (const auto &checkpoint : checkpoints_)
      total += checkpoint.label.capacity();
    return total;
  }

  std::string serialize() const;
  static std::optional<ReplayRecorder> parse(std::string_view document,
                                             std::string *error = nullptr);

private:
  ReplayHeader header_;
  std::vector<ReplayCommand> commands_;
  std::vector<ReplayCheckpoint> checkpoints_;
};

// Divergence localization helpers. A single whole-document checkpoint
// hash only reports "state differs at tick N"; document_section_checkpoints
// emits one labeled checkpoint per top-level member ("<prefix>:<key>")
// plus one per member of an object member ("<prefix>:<key>.<sub>"), so a
// mismatch names the subsystem ("save:Fleets"). Emission order follows
// the document's member order — deterministic for ordered_json encoders.
[[nodiscard]] std::vector<ReplayCheckpoint>
document_section_checkpoints(std::uint64_t tick,
                             const nlohmann::ordered_json &document,
                             std::string_view label_prefix);

struct CheckpointVerification {
  std::size_t verified{};
  std::string divergence; // empty = all entries verified
};

// Compares freshly computed checkpoints against the recording's expected
// sequence starting at `cursor`, advancing it past verified entries.
// Stops at the first mismatch; messages name the checkpoint label.
[[nodiscard]] CheckpointVerification verify_checkpoint_sequence(
    std::span<const ReplayCheckpoint> expected, std::size_t &cursor,
    std::span<const ReplayCheckpoint> actual);

// Streams a recorded command stream back in tick order. Callers pull
// commands_for(tick) inside their fixed-step loop and verify checkpoints.
class ReplayPlayer {
public:
  explicit ReplayPlayer(const ReplayRecorder *recording);

  // Returns recorded commands scheduled exactly at `tick`, in record order.
  std::vector<const ReplayCommand *> commands_for(std::uint64_t tick) const;

  // Checks a freshly computed hash against the checkpoint recorded at
  // `tick`. Returns nullopt when no checkpoint exists there; otherwise the
  // expected hash (compare against `actual` — a mismatch means divergence).
  std::optional<std::uint64_t> expected_checkpoint(std::uint64_t tick) const;

  const ReplayHeader &header() const;

private:
  const ReplayRecorder *recording_;
};

} // namespace stellar::engine
