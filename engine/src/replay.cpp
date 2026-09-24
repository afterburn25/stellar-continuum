#include <stellar/engine/replay.hpp>

#include <nlohmann/json.hpp>

#include <utility>

namespace stellar::engine {

std::uint64_t fnv1a64(std::string_view bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char c : bytes) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

ReplayRecorder::ReplayRecorder(ReplayHeader header)
    : header_(std::move(header)) {}

void ReplayRecorder::record(std::uint64_t tick, std::string name,
                            std::string payload) {
  if (truncated_) return;
  if (memory_budget_ != 0 &&
      estimated_memory_bytes() + sizeof(ReplayCommand) + name.size() +
              payload.size() >
          memory_budget_) {
    truncated_ = true;
    return;
  }
  commands_.push_back(ReplayCommand{tick, std::move(name),
                                    std::move(payload)});
}

void ReplayRecorder::checkpoint(std::uint64_t tick, std::uint64_t hash,
                                std::string label) {
  if (truncated_) return;
  if (memory_budget_ != 0 &&
      estimated_memory_bytes() + sizeof(ReplayCheckpoint) + label.size() >
          memory_budget_) {
    truncated_ = true;
    return;
  }
  checkpoints_.push_back(ReplayCheckpoint{tick, hash, std::move(label)});
}

std::string ReplayRecorder::serialize() const {
  nlohmann::json doc;
  doc["seed"] = header_.seed;
  doc["build_id"] = header_.build_id;
  doc["game_version"] = header_.game_version;
  if (header_.window_width != 0 || header_.window_height != 0) {
    doc["window_width"] = header_.window_width;
    doc["window_height"] = header_.window_height;
  }
  doc["commands"] = nlohmann::json::array();
  for (const auto &command : commands_)
    doc["commands"].push_back({{"tick", command.tick},
                               {"name", command.name},
                               {"payload", command.payload}});
  doc["checkpoints"] = nlohmann::json::array();
  for (const auto &checkpoint : checkpoints_)
    doc["checkpoints"].push_back({{"tick", checkpoint.tick},
                                  {"hash", checkpoint.hash},
                                  {"label", checkpoint.label}});
  if (truncated_) doc["truncated"] = true;
  return doc.dump();
}

std::optional<ReplayRecorder> ReplayRecorder::parse(std::string_view document,
                                                    std::string *error) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(document);
  } catch (const std::exception &ex) {
    if (error != nullptr)
      *error = ex.what();
    return std::nullopt;
  }
  if (!doc.is_object()) {
    if (error != nullptr)
      *error = "replay document must be an object";
    return std::nullopt;
  }
  ReplayRecorder recorder(ReplayHeader{
      doc.value("seed", std::uint64_t{}), doc.value("build_id", std::string{}),
      doc.value("game_version", std::string{}),
      doc.value("window_width", std::uint32_t{}),
      doc.value("window_height", std::uint32_t{})});
  for (const auto &entry : doc.value("commands", nlohmann::json::array()))
    recorder.record(entry.value("tick", std::uint64_t{}),
                    entry.value("name", std::string{}),
                    entry.value("payload", std::string{}));
  for (const auto &entry :
       doc.value("checkpoints", nlohmann::json::array()))
    recorder.checkpoint(entry.value("tick", std::uint64_t{}),
                        entry.value("hash", std::uint64_t{}),
                        entry.value("label", std::string{}));
  recorder.truncated_ = doc.value("truncated", false);
  return recorder;
}

std::vector<ReplayCheckpoint>
document_section_checkpoints(std::uint64_t tick,
                             const nlohmann::ordered_json &document,
                             std::string_view label_prefix) {
  std::vector<ReplayCheckpoint> out;
  if (!document.is_object())
    return out;
  const auto push = [&](std::string label,
                        const nlohmann::ordered_json &value) {
    out.push_back(
        ReplayCheckpoint{tick, fnv1a64(value.dump()), std::move(label)});
  };
  for (const auto &[key, value] : document.items()) {
    const std::string label = std::string(label_prefix) + ":" + key;
    push(label, value);
    // One level deeper for object members — "save:World.Fleets" is far
    // more actionable than "save:World"; arrays stay whole.
    if (value.is_object())
      for (const auto &[sub, subval] : value.items())
        push(label + "." + sub, subval);
  }
  return out;
}

namespace {

void leaf_diff_walk(std::string_view path, const nlohmann::ordered_json &a,
                    const nlohmann::ordered_json &b,
                    std::vector<LeafDivergence> &out, std::size_t limit) {
  if (out.size() >= limit) return;
  const auto report = [&](std::string leaf_path,
                          const nlohmann::ordered_json *expected,
                          const nlohmann::ordered_json *actual) {
    out.push_back(LeafDivergence{
        std::move(leaf_path),
        expected != nullptr ? expected->dump() : std::string("<absent>"),
        actual != nullptr ? actual->dump() : std::string("<absent>")});
  };
  if (a.is_object() && b.is_object()) {
    for (const auto &[key, value] : a.items()) {
      const auto child = path.empty() ? key : std::string(path) + "." + key;
      const auto it = b.find(key);
      if (it == b.end()) {
        report(child, &value, nullptr);
        if (out.size() >= limit) return;
        continue;
      }
      leaf_diff_walk(child, value, *it, out, limit);
      if (out.size() >= limit) return;
    }
    for (const auto &[key, value] : b.items())
      if (!a.contains(key)) {
        report(path.empty() ? key : std::string(path) + "." + key, nullptr,
               &value);
        if (out.size() >= limit) return;
      }
    return;
  }
  if (a.is_array() && b.is_array()) {
    const auto shared = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < shared; ++i) {
      leaf_diff_walk(std::string(path) + "[" + std::to_string(i) + "]", a[i],
                     b[i], out, limit);
      if (out.size() >= limit) return;
    }
    for (std::size_t i = shared; i < a.size(); ++i) {
      report(std::string(path) + "[" + std::to_string(i) + "]", &a[i], nullptr);
      if (out.size() >= limit) return;
    }
    for (std::size_t i = shared; i < b.size(); ++i) {
      report(std::string(path) + "[" + std::to_string(i) + "]", nullptr, &b[i]);
      if (out.size() >= limit) return;
    }
    return;
  }
  if (a != b) report(std::string(path), &a, &b);
}

} // namespace

std::vector<LeafDivergence>
document_leaf_diff(const nlohmann::ordered_json &expected,
                   const nlohmann::ordered_json &actual, std::size_t limit) {
  std::vector<LeafDivergence> out;
  if (limit == 0) return out;
  leaf_diff_walk("", expected, actual, out, limit);
  return out;
}

CheckpointVerification verify_checkpoint_sequence(
    std::span<const ReplayCheckpoint> expected, std::size_t &cursor,
    std::span<const ReplayCheckpoint> actual) {
  CheckpointVerification result;
  for (const auto &checkpoint : actual) {
    if (cursor >= expected.size()) {
      result.divergence =
          "replay produced an unrecorded checkpoint at tick " +
          std::to_string(checkpoint.tick) + " (" + checkpoint.label + ")";
      break;
    }
    const auto &want = expected[cursor];
    if (want.tick != checkpoint.tick) {
      result.divergence = "replay checkpoint tick diverged: expected " +
                          std::to_string(want.tick) + ", captured " +
                          std::to_string(checkpoint.tick) +
                          (want.label.empty() ? "" : " (" + want.label + ")");
      break;
    }
    if (want.hash != checkpoint.hash) {
      const auto &label =
          want.label.empty() ? checkpoint.label : want.label;
      result.divergence =
          "replay state hash diverged at tick " +
          std::to_string(checkpoint.tick) +
          (label.empty() ? "" : " (" + label + ")");
      break;
    }
    ++cursor;
    ++result.verified;
  }
  return result;
}

ReplayPlayer::ReplayPlayer(const ReplayRecorder *recording)
    : recording_(recording) {}

std::vector<const ReplayCommand *>
ReplayPlayer::commands_for(std::uint64_t tick) const {
  std::vector<const ReplayCommand *> result;
  if (recording_ == nullptr)
    return result;
  for (const auto &command : recording_->commands())
    if (command.tick == tick)
      result.push_back(&command);
  return result;
}

std::optional<std::uint64_t>
ReplayPlayer::expected_checkpoint(std::uint64_t tick) const {
  if (recording_ == nullptr)
    return std::nullopt;
  for (const auto &checkpoint : recording_->checkpoints())
    if (checkpoint.tick == tick)
      return checkpoint.hash;
  return std::nullopt;
}

const ReplayHeader &ReplayPlayer::header() const {
  static const ReplayHeader empty{};
  return recording_ == nullptr ? empty : recording_->header();
}

} // namespace stellar::engine
