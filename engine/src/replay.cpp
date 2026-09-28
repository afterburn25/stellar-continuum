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
  commands_.push_back(ReplayCommand{tick, std::move(name),
                                    std::move(payload)});
}

void ReplayRecorder::checkpoint(std::uint64_t tick, std::uint64_t hash,
                                std::string label) {
  checkpoints_.push_back(ReplayCheckpoint{tick, hash, std::move(label)});
}

std::string ReplayRecorder::serialize() const {
  nlohmann::json doc;
  doc["seed"] = header_.seed;
  doc["build_id"] = header_.build_id;
  doc["game_version"] = header_.game_version;
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
      doc.value("game_version", std::string{})});
  for (const auto &entry : doc.value("commands", nlohmann::json::array()))
    recorder.record(entry.value("tick", std::uint64_t{}),
                    entry.value("name", std::string{}),
                    entry.value("payload", std::string{}));
  for (const auto &entry :
       doc.value("checkpoints", nlohmann::json::array()))
    recorder.checkpoint(entry.value("tick", std::uint64_t{}),
                        entry.value("hash", std::uint64_t{}),
                        entry.value("label", std::string{}));
  return recorder;
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
