#include <stellar/engine/replay.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
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

namespace {

[[nodiscard]] std::string replay_utf8_path(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

[[nodiscard]] std::string replay_json_string(std::string_view value) {
  std::ostringstream out;
  out << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char c : value) {
    switch (c) {
    case '"': out << "\\\""; break;
    case '\\': out << "\\\\"; break;
    case '\b': out << "\\b"; break;
    case '\f': out << "\\f"; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default:
      if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
      else out << static_cast<char>(c);
    }
  }
  out << '"';
  return out.str();
}

} // namespace

std::string replay_info_json(const ReplayRecorder &recording,
                             const std::filesystem::path &path) {
  std::ostringstream out;
  out << "replay_info={\"file\":" << replay_json_string(replay_utf8_path(path))
      << ",\"seed\":" << recording.header().seed
      << ",\"build_id\":" << replay_json_string(recording.header().build_id)
      << ",\"game_version\":"
      << replay_json_string(recording.header().game_version);
  if (recording.header().window_width != 0 ||
      recording.header().window_height != 0)
    out << ",\"window_width\":" << recording.header().window_width
        << ",\"window_height\":" << recording.header().window_height;
  out << ",\"commands\":" << recording.commands().size()
      << ",\"truncated\":" << (recording.truncated() ? "true" : "false")
      << ",\"memory_bytes\":" << recording.estimated_memory_bytes();
  // Ordering integrity: the replay feed and the cursor-based checkpoint
  // verifier both assume non-decreasing ticks — a hand-edited or
  // corrupt recording would silently misbehave, so report it.
  out << ",\"commands_ordered\":"
      << (std::ranges::is_sorted(recording.commands(), {},
                                 &ReplayCommand::tick)
              ? "true"
              : "false")
      << ",\"checkpoints_ordered\":"
      << (std::ranges::is_sorted(recording.checkpoints(), {},
                                 &ReplayCheckpoint::tick)
              ? "true"
              : "false");
  if (!recording.commands().empty()) {
    const auto [lo, hi] =
        std::ranges::minmax(recording.commands(), {}, &ReplayCommand::tick);
    out << ",\"command_ticks\":[" << lo.tick << "," << hi.tick
        << "],\"kinds\":{";
    std::unordered_map<std::string, std::size_t> kinds;
    for (const auto &command : recording.commands()) ++kinds[command.name];
    bool first_kind = true;
    for (const auto &[name, count] : kinds) {
      if (!first_kind) out << ",";
      first_kind = false;
      out << replay_json_string(name) << ":" << count;
    }
    out << "}";
  }
  // Pointer commands carry drawable-pixel positions — any landing
  // outside the recorded drawable can never hit the same UI cell on
  // replay (hand-edited or corrupt fixture), so count them.
  if (recording.header().window_width != 0 ||
      recording.header().window_height != 0) {
    std::size_t out_of_bounds = 0;
    for (const auto &command : recording.commands()) {
      if (command.name != "pointer_button") continue;
      int type = 0;
      float x = 0.f, y = 0.f;
      const char *const begin = command.payload.data();
      const char *const end = begin + command.payload.size();
      const auto pt = std::from_chars(begin, end, type);
      if (pt.ec != std::errc{} || pt.ptr >= end || *pt.ptr != ',') continue;
      const auto px = std::from_chars(pt.ptr + 1, end, x);
      if (px.ec != std::errc{} || px.ptr >= end || *px.ptr != ',') continue;
      if (std::from_chars(px.ptr + 1, end, y).ec != std::errc{}) continue;
      if (x < 0.f || y < 0.f || x >= recording.header().window_width ||
          y >= recording.header().window_height)
        ++out_of_bounds;
    }
    out << ",\"pointer_out_of_bounds\":" << out_of_bounds;
  }
  // Verification coverage: commands past the last recorded checkpoint
  // replay but prove nothing — no capture remains to compare them
  // against. Report the tail so a script can judge how much of the
  // recording is actually verified.
  if (!recording.commands().empty()) {
    const auto last_checkpoint =
        recording.checkpoints().empty()
            ? std::uint64_t{}
            : std::ranges::max(recording.checkpoints(), {},
                               &ReplayCheckpoint::tick)
                  .tick;
    std::size_t tail = 0;
    for (const auto &command : recording.commands())
      if (command.tick > last_checkpoint) ++tail;
    out << ",\"unverified_tail_commands\":" << tail;
  }
  // Checkpoints emit one entry per section per capture — group by tick
  // and report whether the canonical expected document is on disk.
  std::map<std::uint64_t, std::size_t> checkpoint_ticks;
  for (const auto &checkpoint : recording.checkpoints())
    ++checkpoint_ticks[checkpoint.tick];
  const auto expected_dir =
      std::filesystem::path(replay_utf8_path(path) + ".expected");
  std::size_t expected_present = 0;
  out << ",\"checkpoints\":[";
  bool first_tick = true;
  for (const auto &[tick, sections] : checkpoint_ticks) {
    if (!first_tick) out << ",";
    first_tick = false;
    std::error_code ec;
    const bool present = std::filesystem::is_regular_file(
        expected_dir / (std::to_string(tick) + ".json"), ec);
    if (present) ++expected_present;
    out << "{\"tick\":" << tick << ",\"sections\":" << sections
        << ",\"expected_document\":" << (present ? "true" : "false");
    if (present) {
      // Verify the retained document hashes to the recorded section
      // checkpoints — a stale or mismatched sidecar would silently
      // poison a later leaf-diff.
      bool verified = false;
      std::string mismatch;
      if (std::ifstream expected_in{
              expected_dir / (std::to_string(tick) + ".json"),
              std::ios::binary};
          expected_in) {
        std::ostringstream doc_contents;
        doc_contents << expected_in.rdbuf();
        try {
          const auto expected_doc =
              nlohmann::ordered_json::parse(doc_contents.str());
          const auto recomputed =
              document_section_checkpoints(tick, expected_doc, "save");
          std::unordered_map<std::string, std::uint64_t> recorded;
          for (const auto &checkpoint : recording.checkpoints())
            if (checkpoint.tick == tick)
              recorded[checkpoint.label] = checkpoint.hash;
          verified = recorded.size() == recomputed.size();
          // A section-count asymmetry can't name a single label; a
          // hash mismatch names the first diverging section.
          if (!verified && recorded.size() == recomputed.size())
            mismatch = "<section count>";
          for (const auto &section : recomputed) {
            const auto found = recorded.find(section.label);
            if (found == recorded.end() || found->second != section.hash) {
              verified = false;
              mismatch = found == recorded.end()
                             ? "<unrecorded: " + section.label + ">"
                             : section.label;
              break;
            }
          }
        } catch (const std::exception &) {
          // An unparseable sidecar stays unverified.
          mismatch = "<unparseable>";
        }
      }
      out << ",\"expected_verified\":" << (verified ? "true" : "false");
      if (!verified && !mismatch.empty())
        out << ",\"expected_mismatch\":" << replay_json_string(mismatch);
    }
    out << "}";
  }
  out << "],\"expected_dir\":"
      << replay_json_string(replay_utf8_path(expected_dir)) << "}";
  return out.str();
}

} // namespace stellar::engine
