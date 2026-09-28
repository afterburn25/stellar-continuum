#include "native_voice_settings.hpp"

#include "native_menu_style.hpp"
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
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace stellar::native_audio {
namespace {
using namespace stellar::native_map;
using Json = nlohmann::json;

constexpr std::size_t maximum_settings_bytes = 4u * 1024u;
constexpr Color veil{2, 8, 17, 48};
constexpr Color row_fill{20, 57, 84, 230};
constexpr Color selected_fill{24, 91, 132, 245};
constexpr Color track_fill{13, 35, 54, 255};
constexpr Color track_value{82, 190, 222, 255};
constexpr Color border{99, 178, 210, 245};
constexpr Color active{133, 228, 244, 255};

float clamp_unit(float value) noexcept { return std::clamp(value, 0.f, 1.f); }
bool valid_unit(float value) noexcept { return std::isfinite(value) && value >= 0.f && value <= 1.f; }
bool valid_size(int value) noexcept {
  constexpr std::array sizes{14, 18, 22, 26, 32};
  return std::ranges::find(sizes, value) != sizes.end();
}
const char* frequency_name(VoiceFrequency value) noexcept {
  switch (value) {
    case VoiceFrequency::Minimal: return "Minimal";
    case VoiceFrequency::Normal: return "Normal";
    case VoiceFrequency::Frequent: return "Frequent";
  }
  return "Normal";
}
std::string percentage(float value) {
  return std::to_string(static_cast<int>(std::lround(value * 100.f))) + "%";
}
void label(DrawList& draw, UiRect bounds, std::string value, int size,
           Color color = native_menu_style::ink, TextAlign alignment = TextAlign::Left) {
  native_menu_style::text(draw, bounds, std::move(value), size, color, alignment);
}
void toggle(DrawList& draw, UiRect bounds, std::string value, bool selected, int font, float scale) {
  stellar::engine::ui_skin::control(draw,bounds,false,selected,true,scale);
  label(draw, {bounds.x + 12.f * scale, bounds.y + (bounds.height - font * 1.4f) * .5f,
               bounds.width - 54.f * scale, font * 1.5f}, std::move(value), font);
  const UiRect switch_track{bounds.x + bounds.width - 38.f * scale, bounds.y + bounds.height * .5f - 7.f * scale,
                            27.f * scale, 14.f * scale};
  native_menu_style::rounded(draw, switch_track, selected ? track_value : Color{61, 86, 103, 255}, 7.f * scale);
  const float knob_x = selected ? switch_track.x + switch_track.width - 12.f * scale : switch_track.x + 2.f * scale;
  native_menu_style::rounded(draw, {knob_x, switch_track.y + 2.f * scale, 10.f * scale, 10.f * scale},
                             native_menu_style::ink, 5.f * scale);
}
void choice(DrawList& draw, UiRect bounds, std::string name, std::string value, int font, float scale) {
  stellar::engine::ui_skin::control(draw,bounds,false,false,true,scale);
  const float text_y = bounds.y + (bounds.height - font * 1.4f) * .5f;
  label(draw, {bounds.x + 12.f * scale, text_y,
               bounds.width * .48f, font * 1.5f}, std::move(name), font, native_menu_style::muted);
  label(draw, {bounds.x + bounds.width * .5f, text_y,
               bounds.width * .5f - 30.f * scale, font * 1.5f}, std::move(value), font);
  label(draw, {bounds.x + bounds.width - 25.f * scale, text_y,
               14.f * scale, font * 1.5f}, "v", font, native_menu_style::cyan, TextAlign::Center);
}
void slider(DrawList& draw, UiRect track, std::string name, float value, int font, float scale) {
  const UiRect name_bounds{track.x, track.y - 24.f * scale, track.width * .65f, 20.f * scale};
  const UiRect value_bounds{track.x + track.width * .65f, name_bounds.y, track.width * .35f, name_bounds.height};
  label(draw, name_bounds, std::move(name), font, native_menu_style::muted);
  label(draw, value_bounds, percentage(value), font, native_menu_style::ink, TextAlign::Right);
  native_menu_style::rounded(draw, track, track_fill, track.height * .5f);
  if (value > 0.f)
    native_menu_style::rounded(draw, {track.x, track.y, track.width * value, track.height}, track_value, track.height * .5f);
  native_menu_style::rounded(draw, track, border, track.height * .5f, true);
  const UiRect thumb{track.x + track.width * value - 7.f * scale, track.y - 3.f * scale,
                     14.f * scale, track.height + 6.f * scale};
  native_menu_style::rounded(draw, thumb, native_menu_style::ink, 7.f * scale);
}
} // namespace

VoiceSettingsLayout VoiceSettingsLayout::for_viewport(int width, int height) noexcept {
  const float screen_w = static_cast<float>(std::max(width, 1));
  const float screen_h = static_cast<float>(std::max(height, 1));
  const float available_w = std::max(1.f, screen_w - 24.f);
  const float available_h = std::max(1.f, screen_h - 24.f);
  const float requested = std::clamp(std::min(screen_w / 1180.f, screen_h / 800.f), .72f, 1.75f);
  const float scale = std::max(.001f, std::min({requested, available_w / 680.f, available_h / 680.f}));
  const float panel_w = std::max(1.f, std::min(available_w, 680.f * scale));
  const float panel_h = std::max(1.f, std::min(available_h, 680.f * scale));
  const UiRect panel{(screen_w - panel_w) * .5f, (screen_h - panel_h) * .5f, panel_w, panel_h};
  const float x = panel.x + 28.f * scale;
  const float content_w = std::max(1.f, panel.width - 56.f * scale);
  const float row_h = 35.f * scale;
  const float track_h = 10.f * scale;
  const float y = panel.y;
  const float gap = 10.f * scale;
  const float action_w = (content_w - gap) * .5f;
  const float footer_w = (content_w - gap * 2.f) / 3.f;
  return {scale, std::max(11, static_cast<int>(std::lround(14.f * scale))),
          std::max(17, static_cast<int>(std::lround(24.f * scale))), panel,
          {x, y + 20.f * scale, content_w, 31.f * scale},
          {x, y + 59.f * scale, content_w, 39.f * scale},
          {x, y + 105.f * scale, content_w, row_h},
          {x, y + 172.f * scale, content_w, track_h},
          {x, y + 199.f * scale, content_w, row_h},
          {x, y + 241.f * scale, content_w, row_h},
          {x, y + 307.f * scale, content_w, track_h},
          {x, y + 334.f * scale, content_w, row_h},
          {x, y + 401.f * scale, content_w, track_h},
          {x, y + 428.f * scale, content_w, row_h},
          {x, y + 470.f * scale, content_w, row_h},
          {x, y + 515.f * scale, action_w, row_h},
          {x + action_w + gap, y + 515.f * scale, action_w, row_h},
          {x, y + 560.f * scale, footer_w, row_h},
          {x + footer_w + gap, y + 560.f * scale, footer_w, row_h},
          {x + (footer_w + gap) * 2.f, y + 560.f * scale, footer_w, row_h},
          {x, y + 609.f * scale, content_w, 43.f * scale}};
}

NativeVoiceSettings::NativeVoiceSettings(std::filesystem::path path, Apply apply, Action replay, Action stop)
    : path_(std::move(path)), apply_(std::move(apply)), replay_(std::move(replay)), stop_(std::move(stop)) {
  load(); preview();
}

void NativeVoiceSettings::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native voice settings must be used on its owner thread.");
}

void NativeVoiceSettings::load() {
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
    const std::string source(buffer.data(), static_cast<std::size_t>(count));
    if (source.find('\0') != std::string::npos) throw std::runtime_error("settings file contains NUL bytes");
    std::unordered_set<std::string> keys;
    bool duplicate_key{};
    const Json json = Json::parse(source, [&](int depth, Json::parse_event_t event, Json& parsed) {
      if (depth == 1 && event == Json::parse_event_t::key && !keys.insert(parsed.get<std::string>()).second)
        duplicate_key = true;
      return true;
    });
    constexpr std::array required{"schemaVersion", "enabled", "volume", "subtitles", "subtitleSize",
                                  "backgroundOpacity", "speakerLabels", "communicationFilter", "frequency",
                                  "noInterruptions"};
    if (!json.is_object() || json.size() != required.size() || duplicate_key)
      throw std::runtime_error("settings schema is unsupported");
    for (const char* key : required) if (!json.contains(key)) throw std::runtime_error("settings member is missing");
    if (!json.at("schemaVersion").is_number_integer() || json.at("schemaVersion").get<std::int64_t>() != 1 ||
        !json.at("enabled").is_boolean() || !json.at("volume").is_number() ||
        !json.at("subtitles").is_boolean() || !json.at("subtitleSize").is_number_integer() ||
        !json.at("backgroundOpacity").is_number() || !json.at("speakerLabels").is_boolean() ||
        !json.at("communicationFilter").is_number() || !json.at("frequency").is_number_integer() ||
        !json.at("noInterruptions").is_boolean())
      throw std::runtime_error("settings member type is invalid");
    const auto unit = [&](const char* name) {
      const double value = json.at(name).get<double>();
      if (!std::isfinite(value) || value < 0. || value > 1.) throw std::runtime_error("setting is outside 0 through 1");
      return static_cast<float>(value);
    };
    const auto size = json.at("subtitleSize").get<int>();
    const auto frequency = json.at("frequency").get<int>();
    if (!valid_size(size) || frequency < 0 || frequency > 2) throw std::runtime_error("choice setting is outside its range");
    VoicePreferences loaded{json.at("enabled").get<bool>(), unit("volume"), json.at("subtitles").get<bool>(), size,
                            unit("backgroundOpacity"), json.at("speakerLabels").get<bool>(),
                            unit("communicationFilter"), static_cast<VoiceFrequency>(frequency),
                            json.at("noInterruptions").get<bool>()};
    if (!valid_unit(loaded.volume) || !valid_unit(loaded.subtitle_background_opacity) ||
        !valid_unit(loaded.communication_filter)) throw std::runtime_error("setting is outside its range");
    values_ = saved_ = loaded;
  } catch (const std::exception& error) {
    values_ = saved_ = {};
    status_ = "Voice settings were not loaded. Defaults are active.";
    std::cerr << "Voice settings load failed for " << path_.string() << ": " << error.what() << '\n';
  }
}

void NativeVoiceSettings::preview() { if (apply_) apply_(values_); }
void NativeVoiceSettings::open() {
  require_owner(); dropdown_.close(); hover_feedback_.reset(); visible_ = true; dragging_ = Dragged::None;
  viewport_width_ = viewport_height_ = 0; preview();
}
bool NativeVoiceSettings::visible() const { require_owner(); return visible_; }
VoicePreferences NativeVoiceSettings::values() const { require_owner(); return values_; }
VoicePreferences NativeVoiceSettings::saved_values() const { require_owner(); return saved_; }
std::string NativeVoiceSettings::status() const { require_owner(); return status_; }

void NativeVoiceSettings::set_from_track(Dragged dragged, Point point, const VoiceSettingsLayout& layout) {
  const UiRect track = dragged == Dragged::Volume ? layout.volume_track :
                       dragged == Dragged::Background ? layout.background_track : layout.filter_track;
  const float value = clamp_unit((point.x - track.x) / track.width);
  if (dragged == Dragged::Volume) values_.volume = value;
  else if (dragged == Dragged::Background) values_.subtitle_background_opacity = value;
  else values_.communication_filter = value;
  preview();
}

bool NativeVoiceSettings::handle(const InputEvent& event, int width, int height) {
  require_owner();
  if (!visible_) return false;
  if ((viewport_width_ != 0 || viewport_height_ != 0) &&
      (viewport_width_ != width || viewport_height_ != height)) dragging_ = Dragged::None;
  viewport_width_ = width; viewport_height_ = height;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  if(dropdown_.visible()){
    const int id=dropdown_.id();const auto anchor=id==0?layout.subtitle_size:layout.frequency;
    hover_feedback_.update(event,dropdown_.hover_target(event.position,anchor,width,height));
    if(const auto selected=dropdown_.handle(event,anchor,width,height)){
      if(id==0){constexpr std::array sizes{14,18,22,26,32};values_.subtitle_size=sizes[*selected];}
      else values_.frequency=static_cast<VoiceFrequency>(*selected);
      preview();
    }
    return true;
  }
  hover_feedback_.update(event,dragging_==Dragged::None?stellar::native_menu_audio::hit(event.position,{layout.enable_voices,layout.volume_track,layout.subtitles,layout.subtitle_size,layout.background_track,layout.speaker_labels,layout.filter_track,layout.frequency,layout.no_interruptions,layout.replay,layout.stop,layout.defaults,layout.cancel,layout.save}):0);
  if (event.type == InputEventType::PointerCancelled) { dragging_ = Dragged::None; return true; }
  if (event.type == InputEventType::EscapePressed) { cancel(); return true; }
  if (event.type == InputEventType::PointerMove && dragging_ != Dragged::None) {
    set_from_track(dragging_, event.position, layout); return true;
  }
  if (event.type == InputEventType::LeftReleased) { dragging_ = Dragged::None; return true; }
  if (event.type != InputEventType::LeftPressed) return true;
  if (layout.volume_track.contains(event.position)) {
    dragging_ = Dragged::Volume; set_from_track(dragging_, event.position, layout); return true;
  }
  if (layout.background_track.contains(event.position)) {
    dragging_ = Dragged::Background; set_from_track(dragging_, event.position, layout); return true;
  }
  if (layout.filter_track.contains(event.position)) {
    dragging_ = Dragged::Filter; set_from_track(dragging_, event.position, layout); return true;
  }
  dragging_ = Dragged::None;
  if (layout.enable_voices.contains(event.position)) values_.enabled = !values_.enabled;
  else if (layout.subtitles.contains(event.position)) values_.subtitles = !values_.subtitles;
  else if (layout.subtitle_size.contains(event.position)) {
    constexpr std::array sizes{14,18,22,26,32};const auto found=std::ranges::find(sizes,values_.subtitle_size);
    dropdown_.open(0,{"14 px","18 px","22 px","26 px","32 px"},static_cast<int>(found-sizes.begin()));return true;
  }
  else if (layout.speaker_labels.contains(event.position)) values_.speaker_labels = !values_.speaker_labels;
  else if (layout.frequency.contains(event.position)) {dropdown_.open(1,{"Minimal","Normal","Frequent"},static_cast<int>(values_.frequency));return true;}
  else if (layout.no_interruptions.contains(event.position)) values_.no_interruptions = !values_.no_interruptions;
  else if (layout.replay.contains(event.position)) { if (replay_) replay_(); return true; }
  else if (layout.stop.contains(event.position)) { if (stop_) stop_(); return true; }
  else if (layout.defaults.contains(event.position)) {
    values_ = {}; status_ = "Default voice and subtitle settings previewed.";
  } else if (layout.cancel.contains(event.position)) { cancel(); return true; }
  else if (layout.save.contains(event.position)) { save(); return true; }
  else return true;
  preview();
  return true;
}

void NativeVoiceSettings::save() {
  try {
    const Json json{{"schemaVersion", 1}, {"enabled", values_.enabled}, {"volume", values_.volume},
                    {"subtitles", values_.subtitles}, {"subtitleSize", values_.subtitle_size},
                    {"backgroundOpacity", values_.subtitle_background_opacity},
                    {"speakerLabels", values_.speaker_labels}, {"communicationFilter", values_.communication_filter},
                    {"frequency", static_cast<int>(values_.frequency)}, {"noInterruptions", values_.no_interruptions}};
    const auto text = json.dump();
    if (text.size() > maximum_settings_bytes) throw std::runtime_error("settings payload exceeds 4 KiB");
    stellar::engine::write_file_atomically(path_,
        std::span{reinterpret_cast<const std::byte*>(text.data()), text.size()});
    saved_ = values_; status_ = "Voice and subtitle settings saved."; visible_ = false;
  } catch (const std::exception& error) {
    status_ = "Could not save voice settings. Check the save folder.";
    if (!save_diagnostic_emitted_) {
      save_diagnostic_emitted_ = true;
      std::cerr << "Voice settings save failed for " << path_.string() << ": " << error.what() << '\n';
    }
  }
}

void NativeVoiceSettings::cancel() {
  require_owner(); dropdown_.close(); dragging_ = Dragged::None; values_ = saved_; preview(); visible_ = false;
}

std::string NativeVoiceSettings::tr(std::string_view key, std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) return std::string(locale_->translate(key));
  return std::string(fallback);
}

void NativeVoiceSettings::render(DrawList& draw, int width, int height) const {
  require_owner();
  if (!visible_) return;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  const float scale = layout.scale;
  draw.overlay.emplace_back(FilledRectangle{{0, 0, static_cast<float>(std::max(width, 1)),
                                              static_cast<float>(std::max(height, 1))}, veil});
  native_menu_style::panel(draw, layout.panel, scale);
  label(draw, layout.title, tr("SETTINGS_VOICE_TITLE", "VOICE & SUBTITLES"),
        layout.heading_font_pixels, native_menu_style::gold);
  label(draw, layout.introduction,
        tr("SETTINGS_VOICE_INTRO",
           "British English scientist announcements. Subtitles remain available when speech is disabled."),
        std::max(11, layout.body_font_pixels - 2), native_menu_style::muted);
  toggle(draw, layout.enable_voices, tr("SETTINGS_VOICE_ENABLE", "Enable voices"),
         values_.enabled, layout.body_font_pixels, scale);
  slider(draw, layout.volume_track, tr("SETTINGS_VOICE_VOLUME", "Voice volume"),
         values_.volume, layout.body_font_pixels, scale);
  toggle(draw, layout.subtitles, tr("SETTINGS_VOICE_SUBTITLES", "Subtitles"),
         values_.subtitles, layout.body_font_pixels, scale);
  choice(draw, layout.subtitle_size, tr("SETTINGS_VOICE_SUBTITLE_SIZE", "Subtitle size"),
         std::to_string(values_.subtitle_size) + " px", layout.body_font_pixels, scale);
  slider(draw, layout.background_track,
         tr("SETTINGS_VOICE_SUBTITLE_BACKGROUND", "Subtitle background"),
         values_.subtitle_background_opacity, layout.body_font_pixels, scale);
  toggle(draw, layout.speaker_labels, tr("SETTINGS_VOICE_SPEAKER_LABELS", "Speaker labels"),
         values_.speaker_labels, layout.body_font_pixels, scale);
  slider(draw, layout.filter_track, tr("SETTINGS_VOICE_COMMS_FILTER", "Communication filter"),
         values_.communication_filter, layout.body_font_pixels, scale);
  choice(draw, layout.frequency, tr("SETTINGS_VOICE_FREQUENCY", "Announcement frequency"),
         frequency_name(values_.frequency), layout.body_font_pixels, scale);
  toggle(draw, layout.no_interruptions,
         tr("SETTINGS_VOICE_NO_INTERRUPTIONS", "Do not interrupt dialogue"),
         values_.no_interruptions, layout.body_font_pixels, scale);
  native_menu_style::button(draw, layout.replay,
                            tr("SETTINGS_VOICE_REPLAY", "Replay last announcement"),
                            layout.body_font_pixels, false, static_cast<bool>(replay_), scale);
  native_menu_style::button(draw, layout.stop, tr("SETTINGS_VOICE_STOP", "Stop"),
                            layout.body_font_pixels, false, static_cast<bool>(stop_), scale);
  native_menu_style::button(draw, layout.defaults, tr("SETTINGS_DEFAULTS", "Defaults"),
                            layout.body_font_pixels, false, true, scale);
  native_menu_style::button(draw, layout.cancel, tr("SETTINGS_CANCEL", "Cancel"),
                            layout.body_font_pixels, false, true, scale);
  native_menu_style::button(draw, layout.save, tr("SETTINGS_SAVE", "Save"),
                            layout.body_font_pixels, true, true, scale);
  label(draw, layout.status, status_.empty() ? tr("SETTINGS_VOICE_HINT", "Changes preview immediately. Save keeps them; Cancel restores the saved settings.")
                                             : status_,
        std::max(11, layout.body_font_pixels - 2), native_menu_style::muted);
  if(dropdown_.visible())dropdown_.render(draw,dropdown_.id()==0?layout.subtitle_size:layout.frequency,width,height,layout.body_font_pixels);
}

} // namespace stellar::native_audio
