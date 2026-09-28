#pragma once

#include <cstdint>
#include <optional>
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

  std::string serialize() const;
  static std::optional<ReplayRecorder> parse(std::string_view document,
                                             std::string *error = nullptr);

private:
  ReplayHeader header_;
  std::vector<ReplayCommand> commands_;
  std::vector<ReplayCheckpoint> checkpoints_;
};

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
