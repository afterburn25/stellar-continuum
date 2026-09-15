#include "native_audio.hpp"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include <dr_mp3/dr_mp3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace stellar::native_audio {
namespace {

std::uint32_t u32le(const char *p) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}
std::uint16_t u16le(const char *p) {
  return static_cast<std::uint16_t>(
      static_cast<unsigned char>(p[0]) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8));
}
float clampf(float v) { return std::clamp(v, -1.f, 1.f); }

// Resample interleaved PCM to the 48 kHz stereo output graph with linear
// interpolation. Deliberately simple — these are bounded UI/music assets.
PcmData to_output_format(const PcmData &in) {
  PcmData out;
  out.sample_rate = output_sample_rate;
  out.channels = output_channels;
  if (in.sample_rate <= 0 || in.channels <= 0 || in.frames.empty()) return out;
  const auto in_frames = in.frames.size() / static_cast<std::size_t>(in.channels);
  const auto out_frames = static_cast<std::size_t>(
      std::ceil(static_cast<double>(in_frames) * output_sample_rate /
                in.sample_rate));
  out.frames.resize(out_frames * output_channels);
  for (std::size_t i = 0; i < out_frames; ++i) {
    const auto position =
        static_cast<double>(i) * in.sample_rate / output_sample_rate;
    const auto base = std::min(static_cast<std::size_t>(position),
                               in_frames > 0 ? in_frames - 1 : 0);
    const auto next = std::min(base + 1, in_frames - 1);
    const auto t = static_cast<float>(position - base);
    for (int c = 0; c < output_channels; ++c) {
      const auto source = std::min(c, in.channels - 1);
      const auto a = in.frames[base * in.channels + source],
                 b = in.frames[next * in.channels + source];
      out.frames[i * output_channels + c] = a + (b - a) * t;
    }
  }
  return out;
}
} // namespace

std::optional<PcmData> decode_wav(std::string_view bytes) {
  if (bytes.size() < 44 || bytes.substr(0, 4) != "RIFF" ||
      bytes.substr(8, 4) != "WAVE")
    return std::nullopt;
  int channels = 0, sample_rate = 0, bits = 0, format = 0;
  std::string_view data;
  std::size_t cursor = 12;
  while (cursor + 8 <= bytes.size()) {
    const auto id = bytes.substr(cursor, 4);
    const auto size = u32le(bytes.data() + cursor + 4);
    const auto payload = cursor + 8;
    if (payload + size > bytes.size()) break;
    if (id == "fmt " && size >= 16) {
      format = u16le(bytes.data() + payload);
      channels = u16le(bytes.data() + payload + 2);
      sample_rate = static_cast<int>(u32le(bytes.data() + payload + 4));
      bits = u16le(bytes.data() + payload + 14);
    } else if (id == "data") {
      data = bytes.substr(payload, size);
    }
    cursor = payload + size + (size & 1u);
  }
  if (!channels || !sample_rate || data.empty()) return std::nullopt;
  PcmData pcm;
  pcm.channels = channels;
  pcm.sample_rate = sample_rate;
  if (format == 3 && bits == 32) {
    pcm.frames.resize(data.size() / 4);
    std::memcpy(pcm.frames.data(), data.data(), pcm.frames.size() * 4);
  } else if (format == 1 && bits == 16) {
    pcm.frames.resize(data.size() / 2);
    for (std::size_t i = 0; i < pcm.frames.size(); ++i)
      pcm.frames[i] =
          static_cast<std::int16_t>(u16le(data.data() + i * 2)) / 32768.f;
  } else if (format == 1 && bits == 24) {
    pcm.frames.resize(data.size() / 3);
    for (std::size_t i = 0; i < pcm.frames.size(); ++i) {
      const auto *p = data.data() + i * 3;
      std::int32_t v = static_cast<unsigned char>(p[0]) |
                       (static_cast<unsigned char>(p[1]) << 8) |
                       (static_cast<unsigned char>(p[2]) << 16);
      if (v & 0x800000) v -= 0x1000000;
      pcm.frames[i] = static_cast<float>(v) / 8388608.f;
    }
  } else if (format == 1 && bits == 8) {
    pcm.frames.resize(data.size());
    for (std::size_t i = 0; i < data.size(); ++i)
      pcm.frames[i] =
          (static_cast<unsigned char>(data[i]) - 128) / 128.f;
  } else {
    return std::nullopt;
  }
  return pcm.frames.empty() ? std::nullopt : std::optional<PcmData>{std::move(pcm)};
}

std::optional<PcmData> decode_mp3(std::string_view bytes) {
  if (bytes.empty()) return std::nullopt;
  drmp3_config config{};
  drmp3_uint64 frame_count = 0;
  float *frames = drmp3_open_memory_and_read_pcm_frames_f32(
      bytes.data(), bytes.size(), &config, &frame_count, nullptr);
  if (!frames || !frame_count) return std::nullopt;
  PcmData pcm{static_cast<int>(config.sampleRate),
              static_cast<int>(config.channels),
              std::vector<float>(frames, frames + frame_count * config.channels)};
  drmp3_free(frames, nullptr);
  return pcm;
}

std::optional<PcmData> decode_audio_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  const std::string bytes{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
  const auto extension = path.extension().string();
  std::optional<PcmData> decoded;
  if (extension == ".mp3")
    decoded = decode_mp3(bytes);
  else if (extension == ".wav")
    decoded = decode_wav(bytes);
  if (!decoded) return std::nullopt;
  auto converted = to_output_format(*decoded);
  if (converted.frames.empty()) return std::nullopt;
  return converted;
}

NativeAudioMixer::NativeAudioMixer(std::filesystem::path settings_path)
    : settings_path_(std::move(settings_path)) {
  if (settings_path_.empty()) return;
  std::ifstream file(settings_path_);
  if (!file) return;
  const std::string text{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};
  const auto read_number = [&text](std::string_view key,
                                   const float fallback) {
    const auto found = text.find(key);
    if (found == std::string::npos) return fallback;
    const auto colon = text.find(':', found + key.size());
    if (colon == std::string::npos) return fallback;
    char *end = nullptr;
    const auto value =
        std::strtof(text.c_str() + colon + 1, &end);
    if (end == text.c_str() + colon + 1) return fallback;
    return std::clamp(value, 0.f, 1.f);
  };
  settings_ = {read_number("\"master\"", settings_.master),
               read_number("\"music\"", settings_.music),
               read_number("\"sfx\"", settings_.sfx)};
}

std::filesystem::path NativeAudioMixer::asset(std::string_view relative) const {
  std::filesystem::path path = asset_root_ / "assets" / "audio";
  for (std::size_t start = 0; start < relative.size();) {
    const auto slash = relative.find('/', start);
    path /= std::string(relative.substr(start, slash - start));
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
  return path;
}

bool NativeAudioMixer::load_assets(const std::filesystem::path &asset_root) {
  asset_root_ = asset_root;
  struct Required {
    NativeSfx id;
    const char *path;
  };
  static const Required required[]{
      {NativeSfx::ui_hover, "sfx/ui-hover.wav"},
      {NativeSfx::ui_confirm, "sfx/ui-confirm.wav"},
      {NativeSfx::discovery_reveal, "sfx/discovery-reveal.wav"},
      {NativeSfx::construction_complete, "sfx/construction-complete.wav"},
      {NativeSfx::ship_launch, "sfx/ship-launch.wav"},
      {NativeSfx::strategic_alert, "sfx/strategic-alert.wav"}};
  music_.reset();
  std::lock_guard lock(voices_mutex_);
  voices_.clear();
  std::map<NativeSfx, std::shared_ptr<const PcmData>> streams;
  for (const auto &[id, path] : required) {
    auto decoded = decode_audio_file(asset(path));
    if (!decoded) return false;
    streams[id] = std::make_shared<const PcmData>(std::move(*decoded));
  }
  auto music = decode_audio_file(asset("music/claimed-by-the-void-loop.mp3"));
  if (!music) return false;
  music_ = std::make_shared<const PcmData>(std::move(*music));
  sfx_streams_ = std::move(streams);
  return true;
}

bool NativeAudioMixer::has_required_audio() const noexcept {
  return music_ != nullptr && sfx_streams_.size() == 6;
}
bool NativeAudioMixer::music_playing() const noexcept { return music_playing_; }
std::size_t NativeAudioMixer::active_voices() const noexcept {
  std::lock_guard lock(voices_mutex_);
  return voices_.size();
}
std::size_t NativeAudioMixer::music_frame_count() const noexcept {
  return music_ ? music_->frames.size() / output_channels : 0;
}
void NativeAudioMixer::complete_startup_loading() {
  startup_ready_ = true;
  if (!music_playing_ && music_) music_playing_ = true;
}
void NativeAudioMixer::set_menu_context(const bool menu) noexcept {
  menu_context_ = menu;
  if (startup_ready_ && !music_playing_ && music_) music_playing_ = true;
}
void NativeAudioMixer::set_voice_ducking(const bool active) noexcept {
  voice_duck_target_ = active ? .55f : 1.f;
}
void NativeAudioMixer::set_volumes(const float master, const float music,
                                   const float sfx) {
  settings_ = {std::clamp(master, 0.f, 1.f), std::clamp(music, 0.f, 1.f),
               std::clamp(sfx, 0.f, 1.f)};
  persist_settings();
}
NativeAudioSettings NativeAudioMixer::settings() const noexcept {
  return settings_;
}
void NativeAudioMixer::persist_settings() const noexcept {
  if (settings_path_.empty()) return;
  std::error_code error;
  std::filesystem::create_directories(settings_path_.parent_path(), error);
  std::ofstream file(settings_path_, std::ios::trunc);
  if (!file) return;
  file << "{\"master\":" << settings_.master << ",\"music\":"
       << settings_.music << ",\"sfx\":" << settings_.sfx << "}\n";
}

void NativeAudioMixer::play(const NativeSfx sound) {
  const auto found = sfx_streams_.find(sound);
  if (found == sfx_streams_.end() || !found->second) return;
  std::lock_guard lock(voices_mutex_);
  if (voices_.size() >= maximum_sfx_voices) return;
  voices_.push_back({found->second, 0});
}
void NativeAudioMixer::play_hover() {
  const auto now = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  if (has_last_hover_ &&
      static_cast<double>(now - last_hover_ms_) < 60.)
    return;
  has_last_hover_ = true;
  last_hover_ms_ = now;
  play(NativeSfx::ui_hover);
}
std::optional<NativeSfx> NativeAudioMixer::event_sound(std::string_view category) {
  std::string lowered(category);
  for (auto &c : lowered)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lowered == "research" || lowered == "exploration" || lowered == "colony")
    return NativeSfx::discovery_reveal;
  if (lowered == "industry" || lowered == "construction")
    return NativeSfx::construction_complete;
  if (lowered == "ships") return NativeSfx::ship_launch;
  if (lowered == "combat") return NativeSfx::strategic_alert;
  return NativeSfx::ui_confirm;
}
void NativeAudioMixer::play_event(std::string_view category) {
  if (const auto sound = event_sound(category)) play(*sound);
}

void NativeAudioMixer::mix(float *output, const std::size_t frame_count) {
  std::fill(output, output + frame_count * output_channels, 0.f);
  const auto step = 1.6f / output_sample_rate;
  const auto music_gain = settings_.master * settings_.music;
  const auto sfx_gain = settings_.master * settings_.sfx;
  std::lock_guard lock(voices_mutex_);
  for (std::size_t i = 0; i < frame_count; ++i) {
    if (voice_duck_ < voice_duck_target_)
      voice_duck_ = std::min(voice_duck_ + step, voice_duck_target_);
    else if (voice_duck_ > voice_duck_target_)
      voice_duck_ = std::max(voice_duck_ - step, voice_duck_target_);
    float left = 0, right = 0;
    if (music_playing_ && music_ && !music_->frames.empty()) {
      const auto frame = (music_cursor_ % (music_->frames.size() / 2)) * 2;
      left += music_->frames[frame] * music_gain * voice_duck_;
      right += music_->frames[frame + 1] * music_gain * voice_duck_;
      ++music_cursor_;
    }
    for (auto &voice : voices_) {
      const auto &frames = voice.source->frames;
      const auto frame = (voice.cursor * 2) % frames.size();
      left += frames[frame] * sfx_gain;
      right += frames[frame + 1] * sfx_gain;
      ++voice.cursor;
    }
    output[i * 2] = clampf(left);
    output[i * 2 + 1] = clampf(right);
  }
  std::erase_if(voices_, [](const Voice &voice) {
    return !voice.source ||
           voice.cursor >= voice.source->frames.size() / output_channels;
  });
}
} // namespace stellar::native_audio
