#include "native_voice_playback.hpp"

#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <stdexcept>

namespace stellar::native_voice {
namespace {

using Clock = std::chrono::system_clock;
constexpr std::size_t queue_limit = 8, per_category_max = 2,
                      recent_limit = 128;
constexpr double recent_window = 15., queued_ttl = 40.;

[[nodiscard]] std::string hex(const std::array<std::uint8_t, 32> &bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (const auto byte : bytes) {
    out += digits[byte >> 4];
    out += digits[byte & 0x0f];
  }
  return out;
}

[[nodiscard]] std::string role_words(VoiceSpeakerRole role) {
  std::string out;
  for (const char c : speaker_role_name(role)) {
    if (!out.empty() && std::isupper(static_cast<unsigned char>(c)) &&
        std::islower(static_cast<unsigned char>(out.back())))
      out += ' ';
    out += c;
  }
  return out;
}

} // namespace

NativeVoiceCache::NativeVoiceCache(std::filesystem::path directory,
                                   long long limit_bytes)
    : directory_(std::filesystem::absolute(std::move(directory))),
      limit_bytes_(limit_bytes) {
  if (limit_bytes_ < 48)
    throw std::invalid_argument("Voice cache limit is too small.");
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
}

std::filesystem::path
NativeVoiceCache::path_for(const NativeVoiceProfile &profile,
                           std::string_view normalized) const {
  const std::string identity =
      "native-v1|voice-dsp-v2|" + profile.id + "|sapi|" +
      profile.preferred_voice.value_or("") + "|" + profile.culture + "|" +
      std::to_string(profile.rate) + "|" + std::to_string(profile.pitch) + "|" +
      (profile.radio ? "1" : "0") + (profile.synthetic ? "1" : "0") + "|" +
      std::string(normalized);
  return directory_ / (hex(stellar::core::detail::adaptive_research_sha256(
                           std::span<const std::uint8_t>(
                               reinterpret_cast<const std::uint8_t *>(
                                   identity.data()),
                               identity.size()))) +
                       ".wav");
}

bool NativeVoiceCache::is_valid_wave(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) ||
      std::filesystem::file_size(path, error) < 48)
    return false;
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  auto read_u32 = [&](std::uint32_t &value) {
    char bytes[4];
    if (!input.read(bytes, 4)) return false;
    value = static_cast<std::uint32_t>(
        static_cast<unsigned char>(bytes[0]) |
        static_cast<unsigned char>(bytes[1]) << 8u |
        static_cast<unsigned char>(bytes[2]) << 16u |
        static_cast<unsigned char>(bytes[3]) << 24u);
    return true;
  };
  auto read_u16 = [&](std::uint16_t &value) {
    char bytes[2];
    if (!input.read(bytes, 2)) return false;
    value = static_cast<std::uint16_t>(
        static_cast<unsigned char>(bytes[0]) |
        static_cast<unsigned char>(bytes[1]) << 8u);
    return true;
  };
  char magic[4];
  if (!input.read(magic, 4) || std::string_view(magic, 4) != "RIFF")
    return false;
  std::uint32_t declared = 0;
  if (!read_u32(declared) || !input.read(magic, 4) ||
      std::string_view(magic, 4) != "WAVE")
    return false;
  const auto length = static_cast<long long>(declared) + 8;
  if (length > static_cast<long long>(std::filesystem::file_size(path, error)))
    return false;
  bool format_valid = false, data_valid = false;
  std::uint16_t alignment = 0;
  while (input.tellg() >= 0 &&
         static_cast<std::streamoff>(input.tellg()) + 8 <= length) {
    char id[4];
    std::uint32_t size = 0;
    if (!input.read(id, 4) || !read_u32(size)) break;
    const auto next = static_cast<std::streamoff>(input.tellg()) +
                      static_cast<std::streamoff>(size);
    if (next > length) return false;
    if (std::string_view(id, 4) == "fmt ") {
      if (size < 16) return false;
      std::uint16_t format = 0, channels = 0, bits = 0;
      std::uint32_t rate = 0, byte_rate = 0;
      if (!read_u16(format) || !read_u16(channels) || !read_u32(rate) ||
          !read_u32(byte_rate) || !read_u16(alignment) || !read_u16(bits))
        return false;
      format_valid = format == 1 && (channels == 1 || channels == 2) &&
                     rate >= 8000 && rate <= 192000 &&
                     (bits == 8 || bits == 16) &&
                     alignment == channels * bits / 8 &&
                     byte_rate == rate * alignment;
    }
    if (std::string_view(id, 4) == "data")
      data_valid = size > 0 && alignment > 0 && size % alignment == 0;
    input.seekg(next + (size & 1));
  }
  return format_valid && data_valid;
}

bool NativeVoiceCache::try_get_valid(
    const std::filesystem::path &path) const {
  if (!is_valid_wave(path)) {
    std::error_code error;
    std::filesystem::remove(path, error);
    return false;
  }
  return true;
}

void NativeVoiceCache::trim() const {
  std::error_code error;
  std::vector<std::filesystem::directory_entry> files;
  for (const auto &entry : std::filesystem::directory_iterator(directory_, error))
    if (entry.is_regular_file() && entry.path().extension() == ".wav")
      files.push_back(entry);
  long long total = 0;
  for (const auto &file : files) total += file.file_size(error);
  std::ranges::sort(files, [](const auto &a, const auto &b) {
    std::error_code ea, eb;
    const auto at = std::filesystem::last_write_time(a, ea);
    const auto bt = std::filesystem::last_write_time(b, eb);
    if (at != bt) return at < bt;
    return a.path().filename() < b.path().filename();
  });
  for (const auto &file : files) {
    if (total <= limit_bytes_) break;
    const auto size = file.file_size(error);
    if (std::filesystem::remove(file.path(), error)) total -= size;
  }
}

NativeVoicePlayback::NativeVoicePlayback(
    NativeVoiceSettings settings, const NativeVoiceProfileRegistry *profiles,
    const NativeCharacterVoiceResolver *speakers, NativeVoiceCache *cache)
    : settings_(settings.sanitized()), profiles_(profiles),
      speakers_(speakers), cache_(cache) {
  if (!profiles_) throw std::invalid_argument("Voice profiles are required.");
}

NativeVoicePlayback::~NativeVoicePlayback() {
  alive_ = false;
  cancel_line_ = true;
  if (backend_) backend_->clear_pending();
}

void NativeVoicePlayback::attach_backend(
    std::unique_ptr<INativeSpeechBackend> backend) {
  backend_ = std::move(backend);
}

void NativeVoicePlayback::bind(Decode decode, Play play, Stop stop) {
  decode_ = std::move(decode);
  play_ = std::move(play);
  stop_play_ = std::move(stop);
}

void NativeVoicePlayback::speak(NativeSpeechRequest request) {
  if (!alive_ || request.text.empty() ||
      std::ranges::all_of(request.text,
                          [](unsigned char c) { return std::isspace(c); }))
    return;
  if (settings_.chatter_level <= 0 &&
      request.priority < static_cast<int>(VoicePriority::Important))
    return;
  // Direct callers that bypass the router still get its expiry rule.
  if (request.expires_at == Clock::time_point{})
    request.expires_at = Clock::now() + std::chrono::seconds(
        request.priority >= static_cast<int>(VoicePriority::Critical) ? 15
                                                                    : 40);
  const std::string key = !request.dedupe_key.empty()
                              ? request.dedupe_key
                              : request.profile_id + "|" + request.text;
  for (auto it = recent_.begin(); it != recent_.end();)
    if (time_ - it->second > recent_window)
      it = recent_.erase(it);
    else
      ++it;
  if (recent_.contains(key)) return;
  if (recent_.size() >= recent_limit)
    recent_.erase(
        std::ranges::min_element(recent_, {},
                                 [](const auto &p) { return p.second; }));
  recent_[key] = time_;
  if (active_ && request.queue_behavior != SpeechQueueBehavior::Enqueue &&
      request.priority > active_->priority && !settings_.no_interruptions &&
      active_->interruptible)
    stop_current();
  if (request.queue_behavior == SpeechQueueBehavior::ReplaceCategory)
    std::erase_if(queue_, [&](const Queued &entry) {
      return entry.request.category == request.category;
    });
  if (queue_.size() >= queue_limit) {
    const auto lowest = std::ranges::min_element(
        queue_, {}, [](const Queued &entry) { return entry.request.priority; });
    if (lowest->request.priority >= request.priority) return;
    queue_.erase(lowest);
  }
  if (std::ranges::count_if(queue_, [&](const Queued &entry) {
        return entry.request.category == request.category;
      }) >= static_cast<std::ptrdiff_t>(per_category_max))
    return;
  queue_.push_back({std::move(request), time_});
}

void NativeVoicePlayback::update(double delta) {
  if (!alive_) return;
  time_ += delta;
  if (synthesis_pending_) {
    if (synthesis_.valid() &&
        synthesis_.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
      synthesis_pending_ = false;
      NativeVoiceResult result;
      try {
        result = synthesis_.get();
      } catch (const std::exception &error) {
        result.error = error.what();
      }
      present_result(std::move(result));
    }
  } else if (active_) {
    remaining_ -= delta;
    if (remaining_ <= 0) finish_line();
  } else {
    const auto now = Clock::now();
    std::erase_if(queue_, [&](const Queued &entry) {
      return time_ - entry.added > queued_ttl || entry.request.expires_at <= now;
    });
    if (!queue_.empty()) {
      const auto next = std::ranges::min_element(
          queue_,
          [](const Queued &a, const Queued &b) {
            return a.request.priority != b.request.priority
                       ? a.request.priority > b.request.priority
                       : a.added < b.added;
          });
      auto request = next->request;
      queue_.erase(next);
      begin_line(std::move(request));
    }
  }
}

void NativeVoicePlayback::begin_line(NativeSpeechRequest request) {
  if (request.speaker_context && speakers_) {
    const auto resolved = speakers_->resolve(*request.speaker_context);
    request.profile_id = resolved ? resolved->profile_id : "";
    request.speaker_name = resolved ? resolved->display_name
                                    : role_words(request.speaker_context->role);
    request.speaker_character_id = resolved ? resolved->character_id
                                            : std::nullopt;
    request.speaker_portrait = resolved ? resolved->portrait : std::nullopt;
    diagnostics_ = "Event: " + request.event_id + "\nRole: " +
                   std::string(speaker_role_name(
                       request.speaker_context->role)) +
                   "\nVoice: " +
                   (resolved ? resolved->profile_id : "subtitles only");
  }
  active_ = last_ = request;
  // Approved prerecorded cues bypass synthesis entirely and play through the
  // same decode/present path as backend output.
  if (settings_.enable_voices && request.prerecorded_path && decode_) {
    present_result(
        NativeVoiceResult{true, *request.prerecorded_path, {}, false, {}});
    return;
  }
  if (settings_.enable_voices && backend_ && backend_->available()) {
    const auto *profile = profiles_->try_resolve(request.profile_id);
    if (profile) {
      cancel_line_ = false;
      std::filesystem::path path;
      if (cache_) {
        path = cache_->path_for(*profile, request.text);
        if (cache_->try_get_valid(path)) {
          present_result(
              NativeVoiceResult{true, path, {}, true, {}});
          return;
        }
      }
      synthesis_ = backend_->synthesize(*profile, request.text,
                                        std::move(path));
      synthesis_pending_ = true;
      diagnostics_ = "Synthesizing " + request.profile_id + " · queued " +
                     std::to_string(queue_.size());
      return;
    }
  }
  present_fallback(settings_.enable_voices ? "Backend unavailable"
                                         : "Voice disabled");
}

void NativeVoicePlayback::present_result(NativeVoiceResult result) {
  if (!active_) return;
  if (!settings_.enable_voices) {
    speaking_ = false;
    present_fallback("Voice disabled while synthesis was pending");
    return;
  }
  if (!result.succeeded || result.wave_path.empty() || !decode_) {
    present_fallback(result.error.empty() ? "Speech unavailable" : result.error);
    return;
  }
  const auto stream = decode_(result.wave_path);
  if (!stream) {
    present_fallback("Invalid audio returned by backend");
    return;
  }
  const auto seconds =
      stream->sample_rate > 0 && stream->channels > 0
          ? static_cast<double>(stream->frames.size()) /
                (stream->sample_rate * stream->channels)
          : 0.;
  if (seconds <= 0) {
    present_fallback("Invalid audio returned by backend");
    return;
  }
  if (play_) play_(stream, seconds);
  last_source_ = active_->prerecorded_path
                     ? "recorded"
                     : (result.cache_hit ? "cache" : "synthesized");
  diagnostics_ = active_->profile_id + " · " +
                 (result.selected_voice.empty() ? "installed voice"
                                                : result.selected_voice) +
                 " · " + last_source_;
  remaining_ = std::max(seconds + .25, read_seconds(*active_));
  if (active_->speaker_name) active_speaker_ = *active_->speaker_name;
  active_portrait_ = active_->speaker_portrait;
  active_text_ = subtitle_for(*active_);
  ++played_lines_;
  ++subtitle_lines_;
  speaking_ = true;
}

void NativeVoicePlayback::present_fallback(std::string_view reason) {
  if (!active_) return;
  last_source_ = "subtitle";
  diagnostics_ = active_->profile_id + " · subtitle only · " +
                 std::string(reason);
  remaining_ = read_seconds(*active_);
  if (active_->speaker_name) active_speaker_ = *active_->speaker_name;
  active_portrait_ = active_->speaker_portrait;
  active_text_ = subtitle_for(*active_);
  ++subtitle_lines_;
}

std::string
NativeVoicePlayback::subtitle_for(const NativeSpeechRequest &line) const {
  if (!line.localization_key.empty() && locale_ &&
      locale_->contains(line.localization_key))
    return std::string(locale_->translate(line.localization_key));
  return line.subtitle_text ? *line.subtitle_text : line.text;
}

void NativeVoicePlayback::finish_line() {
  active_ = std::nullopt;
  active_text_.clear();
  active_speaker_.clear();
  active_portrait_ = std::nullopt;
  speaking_ = false;
}

void NativeVoicePlayback::stop_current() {
  cancel_line_ = true;
  synthesis_pending_ = false;
  if (synthesis_.valid()) {
    // The detached backend task may still complete; drop its result.
    synthesis_ = {};
  }
  if (backend_) backend_->clear_pending();
  if (stop_play_) stop_play_();
  finish_line();
}

void NativeVoicePlayback::stop() {
  stop_current();
  queue_.clear();
}

void NativeVoicePlayback::replay_last() {
  if (!last_) return;
  auto last = *last_;
  stop();
  recent_.clear();
  last.dedupe_key = "replay:" + std::to_string(time_);
  last.expires_at =
      std::chrono::system_clock::now() + std::chrono::seconds(40);
  speak(std::move(last));
}

void NativeVoicePlayback::reset_campaign() {
  stop();
  recent_.clear();
  last_ = std::nullopt;
}

double NativeVoicePlayback::read_seconds(const NativeSpeechRequest &request) {
  const std::string &text =
      request.subtitle_text ? *request.subtitle_text : request.text;
  const auto words = static_cast<double>(
      std::ranges::count_if(text, [](unsigned char c) {
        return std::isspace(c);
      }) + 1);
  return std::clamp(words / 2.7 + 1., 3., 30.);
}

bool NativeVoicePlayback::is_speaking() const noexcept {
  return active_.has_value() || synthesis_pending_;
}

bool NativeVoicePlayback::has_active_subtitle() const noexcept {
  return active_ && !synthesis_pending_ && settings_.subtitles;
}

std::string NativeVoicePlayback::active_subtitle() const {
  return has_active_subtitle() ? active_text_ : "";
}

std::string NativeVoicePlayback::active_speaker_name() const {
  return has_active_subtitle() && settings_.speaker_labels ? active_speaker_
                                                           : "";
}

std::optional<std::string> NativeVoicePlayback::active_speaker_portrait()
    const {
  return has_active_subtitle() ? active_portrait_ : std::nullopt;
}

int NativeVoicePlayback::pending_count() const noexcept {
  return static_cast<int>(queue_.size());
}

std::string NativeVoicePlayback::diagnostics() const { return diagnostics_; }

std::string NativeVoicePlayback::backend_status() const {
  if (!backend_) return "Backend: none";
  return "Backend: " + backend_->backend_id() +
         (backend_->available() ? " · " + backend_->detail()
                                : " · unavailable");
}

std::string NativeVoicePlayback::last_source() const { return last_source_; }

void NativeVoicePlayback::apply_settings(NativeVoiceSettings settings) {
  const auto voices_were_enabled = settings_.enable_voices;
  settings_ = settings.sanitized();
  if (!settings_.enable_voices) {
    speaking_ = false;
    if (voices_were_enabled && active_ && synthesis_pending_)
      present_fallback("Voice disabled while synthesis was pending");
  }
}

} // namespace stellar::native_voice
