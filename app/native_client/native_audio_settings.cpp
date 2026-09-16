#include "native_audio_settings.hpp"

#include <stellar/engine/atomic_file_write.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <unordered_set>

namespace stellar::native_audio {
namespace {
using namespace stellar::native_map;
using Json = nlohmann::json;

constexpr std::size_t maximum_settings_bytes = 4u * 1024u;
constexpr Color veil{3, 10, 22, 210};
constexpr Color panel_fill{10, 25, 45, 250};
constexpr Color panel_stroke{104, 184, 212, 255};
constexpr Color track_fill{25, 49, 72, 255};
constexpr Color accent{104, 224, 188, 255};
constexpr Color text_color{232, 243, 250, 255};
constexpr Color muted_color{244, 185, 108, 255};

float clamp_gain(float value) noexcept { return std::clamp(value, 0.f, 1.f); }
bool valid_gain(float value) noexcept { return std::isfinite(value) && value >= 0.f && value <= 1.f; }
Point center(UiRect rect) noexcept { return {rect.x + rect.width * .5f, rect.y + rect.height * .5f}; }
void label(DrawList& draw, Point at, std::string value, int size, UiRect clip,
           TextAlign align = TextAlign::Left, FontFace face = FontFace::Interface) {
  draw.overlay.emplace_back(Text{at, std::move(value), text_color, size, clip.width, clip, align, face});
}
void button(DrawList& draw, UiRect rect, std::string value, int size, bool selected = false) {
  draw.overlay.emplace_back(FilledRectangle{rect, selected ? accent : Color{22, 53, 76, 255}});
  draw.overlay.emplace_back(StrokedRectangle{rect, selected ? Color{222, 255, 244, 255} : panel_stroke});
  const auto at = center(rect);
  draw.overlay.emplace_back(Text{{at.x, at.y - static_cast<float>(size) * .5f},
      std::move(value), selected ? panel_fill : text_color, size,
      rect.width, rect, TextAlign::Center});
}
std::string percentage(float gain) { return std::to_string(static_cast<int>(std::lround(gain * 100.f))) + "%"; }
}

AudioSettingsLayout AudioSettingsLayout::for_viewport(int width, int height) noexcept {
  const float screen_w = static_cast<float>(std::max(1, width));
  const float screen_h = static_cast<float>(std::max(1, height));
  const float available_w = std::max(1.f, screen_w - 24.f), available_h = std::max(1.f, screen_h - 24.f);
  const float requested_scale = std::clamp(std::min(screen_w / 720.f, screen_h / 720.f), .72f, 2.6f);
  const float scale = std::max(.001f, std::min({requested_scale, available_w / 560.f, available_h / 484.f}));
  const float panel_w = std::max(1.f, std::min(available_w, 560.f * scale));
  const float panel_h = std::max(1.f, std::min(available_h, 484.f * scale));
  const UiRect panel{(screen_w - panel_w) * .5f, (screen_h - panel_h) * .5f, panel_w, panel_h};
  const float inset = std::min(32.f * scale, panel.width * .12f);
  const float track_x = panel.x + inset, track_w = std::max(1.f, panel.width - inset * 2.f);
  const float row = 74.f * scale, first = panel.y + 132.f * scale;
  const float button_h = 38.f * scale, gap = 10.f * scale, button_w = (track_w - gap) * .5f;
  return {scale, std::max(12, static_cast<int>(std::lround(17.f * scale))),
          std::max(16, static_cast<int>(std::lround(25.f * scale))), panel,
          {track_x, first, track_w, 14.f * scale},
          {track_x, first + row, track_w, 14.f * scale},
          {track_x, first + row * 2.f, track_w, 14.f * scale},
          {track_x, panel.y + panel.height - 144.f * scale, track_w, button_h},
          {track_x, panel.y + panel.height - 96.f * scale, button_w, button_h},
          {track_x + button_w + gap, panel.y + panel.height - 96.f * scale, button_w, button_h},
          {track_x, panel.y + panel.height - 48.f * scale, track_w, button_h},
          {track_x, panel.y + 82.f * scale, track_w, 22.f * scale},
          {track_x + track_w - 122.f * scale, panel.y + 48.f * scale, 122.f * scale, 28.f * scale},
          {track_x, panel.y + 48.f * scale, 122.f * scale, 28.f * scale}};
}

NativeAudioSettings::NativeAudioSettings(std::filesystem::path path, Apply apply, Confirm confirm)
    : path_(std::move(path)), apply_(std::move(apply)), confirm_(std::move(confirm)) { load(); preview(); }

void NativeAudioSettings::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native audio settings must be used on its owner thread.");
}

void NativeAudioSettings::load() {
  try {
    if (!std::filesystem::exists(path_)) { saved_ = values_; return; }
    if (!std::filesystem::is_regular_file(path_)) throw std::runtime_error("settings path is not a regular file");
    std::ifstream input(path_, std::ios::binary);
    if (!input) throw std::runtime_error("settings file could not be opened");
    std::array<char, maximum_settings_bytes + 1> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (input.bad()) throw std::runtime_error("settings file could not be read");
    if (count > static_cast<std::streamsize>(maximum_settings_bytes)) throw std::runtime_error("settings file exceeds 4 KiB");
    const std::string text(buffer.data(), static_cast<std::size_t>(count));
    std::unordered_set<std::string> keys;
    bool duplicate_key{};
    const auto json = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& parsed) {
      if (depth == 1 && event == Json::parse_event_t::key && !keys.insert(parsed.get<std::string>()).second)
        duplicate_key = true;
      return true;
    });
    if (!json.is_object() || json.size() != 5 || !json.contains("schemaVersion") ||
        !json.contains("master") || !json.contains("music") || !json.contains("effects") || !json.contains("muted") ||
        !json.at("schemaVersion").is_number_integer() ||
        !json.at("master").is_number() || !json.at("music").is_number() || !json.at("effects").is_number() ||
        !json.at("muted").is_boolean() || duplicate_key || json.at("schemaVersion").get<std::int64_t>() != 1)
      throw std::runtime_error("settings schema is unsupported");
    const auto gain = [&](const char* name) {
      const double value = json.at(name).get<double>();
      if (!std::isfinite(value) || value < 0. || value > 1.)
        throw std::runtime_error("settings gains must be finite values from 0 through 1");
      return static_cast<float>(value);
    };
    AudioPreferences loaded{gain("master"), gain("music"), gain("effects"), json.at("muted").get<bool>()};
    if (!valid_gain(loaded.master) || !valid_gain(loaded.music) || !valid_gain(loaded.effects))
      throw std::runtime_error("settings gains must be finite values from 0 through 1");
    values_ = saved_ = loaded;
  } catch (const std::exception& error) {
    values_ = saved_ = {};
    status_ = "Audio settings were not loaded. Defaults are active.";
    std::cerr << "Audio settings load failed for " << path_.string() << ": " << error.what() << '\n';
  }
}

void NativeAudioSettings::preview() { if (apply_) apply_(values_); }
void NativeAudioSettings::open() { require_owner(); visible_ = true; dragging_ = Dragged::None; viewport_width_ = viewport_height_ = 0; preview(); }
bool NativeAudioSettings::visible() const { require_owner(); return visible_; }
AudioPreferences NativeAudioSettings::values() const { require_owner(); return values_; }
AudioPreferences NativeAudioSettings::saved_values() const { require_owner(); return saved_; }
std::string NativeAudioSettings::status() const { require_owner(); return status_; }
void NativeAudioSettings::set_device_status(std::string message) { require_owner(); device_status_ = std::move(message); }

void NativeAudioSettings::set_from_track(Dragged drag, Point point, const AudioSettingsLayout& layout) {
  const UiRect track = drag == Dragged::Master ? layout.master_track : drag == Dragged::Music ? layout.music_track : layout.effects_track;
  const float value = clamp_gain((point.x - track.x) / track.width);
  if (drag == Dragged::Master) values_.master = value;
  else if (drag == Dragged::Music) values_.music = value;
  else if (drag == Dragged::Effects) values_.effects = value;
  preview();
}

bool NativeAudioSettings::handle(const InputEvent& event, int width, int height) {
  require_owner();
  if (!visible_) return false;
  if ((viewport_width_ != 0 || viewport_height_ != 0) &&
      (viewport_width_ != width || viewport_height_ != height)) dragging_ = Dragged::None;
  viewport_width_ = width; viewport_height_ = height;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) { dragging_ = Dragged::None; return true; }
  if (event.type == InputEventType::EscapePressed) { cancel(); return true; }
  if (event.type == InputEventType::PointerMove && dragging_ != Dragged::None) { set_from_track(dragging_, event.position, layout); return true; }
  if (event.type == InputEventType::LeftReleased) { dragging_ = Dragged::None; return true; }
  if (event.type != InputEventType::LeftPressed) return true;
  const auto invoke_confirm = [&] { if (confirm_) confirm_(); };
  if (video_navigation_ && layout.video.contains(event.position)) { cancel(); invoke_confirm(); video_navigation_(); return true; }
  if (general_navigation_ && layout.general.contains(event.position)) { cancel(); invoke_confirm(); general_navigation_(); return true; }
  if (layout.master_track.contains(event.position)) { dragging_ = Dragged::Master; set_from_track(dragging_, event.position, layout); return true; }
  if (layout.music_track.contains(event.position)) { dragging_ = Dragged::Music; set_from_track(dragging_, event.position, layout); return true; }
  if (layout.effects_track.contains(event.position)) { dragging_ = Dragged::Effects; set_from_track(dragging_, event.position, layout); return true; }
  dragging_ = Dragged::None;
  if (layout.mute.contains(event.position)) { values_.muted = !values_.muted; preview(); invoke_confirm(); return true; }
  if (layout.defaults.contains(event.position)) { values_ = {}; preview(); status_ = "Default audio levels previewed."; invoke_confirm(); return true; }
  if (layout.cancel.contains(event.position)) { invoke_confirm(); cancel(); return true; }
  if (layout.save.contains(event.position)) { invoke_confirm(); save(); return true; }
  return true;
}

void NativeAudioSettings::save() {
  try {
    const Json json{{"schemaVersion", 1}, {"master", values_.master}, {"music", values_.music},
                    {"effects", values_.effects}, {"muted", values_.muted}};
    const auto text = json.dump();
    if (text.size() > maximum_settings_bytes) throw std::runtime_error("settings payload exceeds 4 KiB");
    const auto bytes = std::span{reinterpret_cast<const std::byte*>(text.data()), text.size()};
    stellar::engine::write_file_atomically(path_, bytes);
    saved_ = values_;
    status_ = "Audio settings saved.";
    visible_ = false;
  } catch (const std::exception& error) {
    status_ = "Could not save audio settings. Check the save folder.";
    if (!save_diagnostic_emitted_) {
      save_diagnostic_emitted_ = true;
      std::cerr << "Audio settings save failed for " << path_.string() << ": " << error.what() << '\n';
    }
  }
}

void NativeAudioSettings::cancel() {
  require_owner();
  dragging_ = Dragged::None;
  values_ = saved_;
  preview();
  visible_ = false;
}

void NativeAudioSettings::render(DrawList& draw, int width, int height) const {
  require_owner();
  if (!visible_) return;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  const UiRect viewport{0, 0, static_cast<float>(std::max(width, 1)), static_cast<float>(std::max(height, 1))};
  draw.overlay.emplace_back(FilledRectangle{viewport, veil});
  draw.overlay.emplace_back(FilledRectangle{layout.panel, panel_fill});
  draw.overlay.emplace_back(StrokedRectangle{layout.panel, panel_stroke});
  const UiRect title_clip{layout.panel.x + 12.f * layout.scale, layout.panel.y + 6.f * layout.scale,
                          layout.panel.width - 24.f * layout.scale, 34.f * layout.scale};
  label(draw, {layout.panel.x + layout.panel.width * .5f,
               std::max(title_clip.y, layout.panel.y + 26.f * layout.scale - static_cast<float>(layout.heading_font_pixels) * .5f)},
        "AUDIO SETTINGS", layout.heading_font_pixels,
        title_clip, TextAlign::Center, FontFace::Heading);
  const std::array rows{std::pair{"Master", layout.master_track}, std::pair{"Music", layout.music_track}, std::pair{"Effects", layout.effects_track}};
  const std::array gains{values_.master, values_.music, values_.effects};
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto track = rows[index].second;
    const UiRect text_rect{track.x, track.y - 27.f * layout.scale, track.width, 22.f * layout.scale};
    label(draw, {text_rect.x, text_rect.y}, rows[index].first, layout.body_font_pixels, text_rect);
    label(draw, {text_rect.x + text_rect.width, text_rect.y}, percentage(gains[index]), layout.body_font_pixels, text_rect, TextAlign::Right);
    draw.overlay.emplace_back(FilledRectangle{track, track_fill});
    draw.overlay.emplace_back(FilledRectangle{{track.x, track.y, track.width * gains[index], track.height}, accent});
    draw.overlay.emplace_back(StrokedRectangle{track, panel_stroke});
    const UiRect thumb{track.x + track.width * gains[index] - 5.f * layout.scale,
                       track.y - 4.f * layout.scale, 10.f * layout.scale,
                       track.height + 8.f * layout.scale};
    draw.overlay.emplace_back(FilledRectangle{thumb, text_color});
    draw.overlay.emplace_back(StrokedRectangle{thumb, panel_stroke});
  }
  button(draw, layout.mute, values_.muted ? "UNMUTE (LEVELS RETAINED)" : "MUTE", layout.body_font_pixels, values_.muted);
  if (video_navigation_) button(draw, layout.video, "VIDEO", layout.body_font_pixels);
  if (general_navigation_) button(draw, layout.general, "GENERAL", layout.body_font_pixels);
  button(draw, layout.defaults, "DEFAULTS", layout.body_font_pixels);
  button(draw, layout.cancel, "CANCEL", layout.body_font_pixels);
  button(draw, layout.save, "SAVE", layout.body_font_pixels, true);
  const auto notice = status_.empty() ? (values_.muted ? "Audio is muted; your levels are retained." : "Changes preview immediately.") : status_;
  label(draw, {layout.status.x, layout.status.y}, general_navigation_&&!device_status_.empty()?"Playback: unavailable; check your audio device.":notice, std::max(12, layout.body_font_pixels - 2), layout.status, TextAlign::Left);
  if (!device_status_.empty()&&!general_navigation_) {
    const UiRect diagnostic{layout.panel.x + 12.f * layout.scale, layout.panel.y + 54.f * layout.scale,
                            std::max(0.f, layout.panel.width - (video_navigation_ ? 168.f : 24.f) * layout.scale), 22.f * layout.scale};
    draw.overlay.emplace_back(Text{{diagnostic.x, diagnostic.y}, "Playback: unavailable; check your audio device.", muted_color,
                                   std::max(12, layout.body_font_pixels - 2), diagnostic.width, diagnostic});
  }
}

} // namespace stellar::native_audio
