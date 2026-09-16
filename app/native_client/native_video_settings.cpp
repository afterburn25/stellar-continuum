#include "native_video_settings.hpp"

#include <stellar/engine/atomic_file_write.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>

namespace stellar::native_video_settings {
namespace {
using namespace stellar::native_map;

// Reference palette (VisualPalette / MainMenuLayer).
constexpr Color panel{9, 20, 37, 250};
constexpr Color button{14, 34, 58, 245};
constexpr Color hover{26, 64, 98, 250};
constexpr Color border{91, 151, 205, 235};
constexpr Color text_primary{235, 244, 255, 255};
constexpr Color text_muted{151, 180, 207, 245};
constexpr Color text_error{239, 172, 146, 255};
constexpr Color gold{230, 190, 105, 255};

constexpr std::size_t maximum_settings_bytes = 64u * 1024u;
constexpr std::array<std::string_view, 4> choice_names = {"DISPLAY", "RESOLUTION", "V-SYNC",
                                                        "FRAME CAP"};
constexpr std::array<std::string_view, 2> display_names = {"Borderless",
                                                         "Exclusive fullscreen"};
constexpr std::array<std::string_view, 3> vsync_names = {"Off", "On",
                                                       "Adaptive"};
constexpr std::array<std::string_view, 5> frame_cap_names = {
    "Automatic", "60 FPS", "120 FPS", "144 FPS", "Unlimited"};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, UiRect bounds, std::string value, Color color, int pixels,
          TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface) {
  const auto x = align == TextAlign::Center ? bounds.x + bounds.width * .5f : bounds.x;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, bounds, align, face});
}

[[nodiscard]] int enum_index(const auto value, const int count) {
  const auto index = static_cast<int>(value);
  return index >= 0 && index < count ? index : 0;
}

[[nodiscard]] std::string_view display_name(const VideoDisplayMode value) {
  return display_names[static_cast<std::size_t>(
      enum_index(value, static_cast<int>(display_names.size())))];
}
[[nodiscard]] std::string_view vsync_name(const VideoVsync value) {
  return vsync_names[static_cast<std::size_t>(
      enum_index(value, static_cast<int>(vsync_names.size())))];
}
[[nodiscard]] std::string_view frame_cap_name(const VideoFrameCap value) {
  return frame_cap_names[static_cast<std::size_t>(
      enum_index(value, static_cast<int>(frame_cap_names.size())))];
}

[[nodiscard]] std::string_view display_key(const VideoDisplayMode value) {
  return value == VideoDisplayMode::Exclusive ? "Exclusive" : "Borderless";
}
[[nodiscard]] std::string_view vsync_key(const VideoVsync value) {
  return value == VideoVsync::Off       ? "Off"
         : value == VideoVsync::Adaptive ? "Adaptive"
                                        : "On";
}
[[nodiscard]] std::string_view frame_cap_key(const VideoFrameCap value) {
  switch (value) {
  case VideoFrameCap::Fps60: return "Fps60";
  case VideoFrameCap::Fps120: return "Fps120";
  case VideoFrameCap::Fps144: return "Fps144";
  case VideoFrameCap::Unlimited: return "Unlimited";
  default: return "Automatic";
  }
}
[[nodiscard]] std::optional<VideoDisplayMode>
parse_display(std::string_view value) noexcept {
  if (value == "Exclusive") return VideoDisplayMode::Exclusive;
  if (value == "Borderless") return VideoDisplayMode::Borderless;
  return std::nullopt;
}
[[nodiscard]] std::optional<VideoVsync> parse_vsync(std::string_view value) noexcept {
  if (value == "Off") return VideoVsync::Off;
  if (value == "Adaptive") return VideoVsync::Adaptive;
  if (value == "On") return VideoVsync::On;
  return std::nullopt;
}
[[nodiscard]] std::optional<VideoFrameCap>
parse_frame_cap(std::string_view value) noexcept {
  if (value == "Fps60") return VideoFrameCap::Fps60;
  if (value == "Fps120") return VideoFrameCap::Fps120;
  if (value == "Fps144") return VideoFrameCap::Fps144;
  if (value == "Unlimited") return VideoFrameCap::Unlimited;
  if (value == "Automatic") return VideoFrameCap::Automatic;
  return std::nullopt;
}

[[nodiscard]] std::string read_string(const nlohmann::json &node,
                                     std::string_view key) {
  const auto found = node.find(std::string(key));
  return found != node.end() && found->is_string()
             ? found->get<std::string>()
             : std::string{};
}
[[nodiscard]] int read_dimension(const nlohmann::json &node, std::string_view key) noexcept {
  const auto found = node.find(std::string(key));
  if (found == node.end() || !found->is_number_integer()) return 0;
  const auto value = found->get<std::int64_t>();
  return value > 0 && value <= 16384 ? static_cast<int>(value) : 0;
}
[[nodiscard]] float read_refresh(const nlohmann::json &node) noexcept {
  const auto found = node.find("refreshHz");
  if (found == node.end() || !found->is_number()) return 0.f;
  const auto value = found->get<double>();
  return std::isfinite(value) && value > 0. && value <= 1000. ? static_cast<float>(value) : 0.f;
}
[[nodiscard]] std::string resolution_name(const NativeVideoSettings &value,
                                          std::string_view desktop) {
  if (value.display != VideoDisplayMode::Exclusive) return "Desktop (" + std::string(desktop) + ")";
  if (value.width <= 0 || value.height <= 0 || !std::isfinite(value.refresh_hz) || value.refresh_hz <= 0.f)
    return "Desktop default";
  return std::to_string(value.width) + " x " + std::to_string(value.height) + " @ " +
         std::to_string(static_cast<int>(std::lround(value.refresh_hz))) + " Hz";
}
} // namespace

NativeVideoSettings NativeVideoSettings::sanitized() const noexcept {
  auto copy = *this;
  if (copy.display != VideoDisplayMode::Borderless &&
      copy.display != VideoDisplayMode::Exclusive)
    copy.display = VideoDisplayMode::Borderless;
  if (copy.vsync != VideoVsync::Off && copy.vsync != VideoVsync::On &&
      copy.vsync != VideoVsync::Adaptive)
    copy.vsync = VideoVsync::On;
  if (enum_index(copy.frame_cap, 5) == 0 &&
      copy.frame_cap != VideoFrameCap::Automatic)
    copy.frame_cap = VideoFrameCap::Automatic;
  if (copy.width < 0 || copy.width > 16384 || copy.height < 0 || copy.height > 16384 ||
      !std::isfinite(copy.refresh_hz) || copy.refresh_hz < 0.f || copy.refresh_hz > 1000.f ||
      (copy.width == 0) != (copy.height == 0) || ((copy.width == 0) != (copy.refresh_hz == 0.f)))
    copy.width = copy.height = 0, copy.refresh_hz = 0.f;
  return copy;
}

bool VideoDisplayChoice::valid() const noexcept {
  return width > 0 && width <= 16384 && height > 0 && height <= 16384 &&
         std::isfinite(refresh_hz) && refresh_hz > 0.f && refresh_hz <= 1000.f;
}

NativeVideoSettings
NativeVideoSettings::load(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return {};
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("video settings could not be opened");
    std::string bytes(maximum_settings_bytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = input.gcount();
    if (input.bad() || count > static_cast<std::streamsize>(maximum_settings_bytes))
      throw std::runtime_error("video settings exceed 64 KiB or could not be read");
    bytes.resize(static_cast<std::size_t>(count));
    const auto document = nlohmann::json::parse(bytes, nullptr, true, true);
    if (!document.is_object()) throw std::runtime_error("video settings root is not an object");
    NativeVideoSettings settings;
    if (const auto display = parse_display(read_string(document, "display")))
      settings.display = *display;
    if (const auto vsync = parse_vsync(read_string(document, "vsync")))
      settings.vsync = *vsync;
    if (const auto cap = parse_frame_cap(read_string(document, "frameCap")))
      settings.frame_cap = *cap;
    settings.width = read_dimension(document, "width");
    settings.height = read_dimension(document, "height");
    settings.refresh_hz = read_refresh(document);
    return settings.sanitized();
  } catch (const std::exception &) {
    return {};
  }
}

void NativeVideoSettings::save(const std::filesystem::path &path) const {
  const auto settings = sanitized();
  nlohmann::json document;
  document["display"] = display_key(settings.display);
  document["vsync"] = vsync_key(settings.vsync);
  document["frameCap"] = frame_cap_key(settings.frame_cap);
  document["width"] = settings.width;
  document["height"] = settings.height;
  document["refreshHz"] = settings.refresh_hz;
  const auto payload = document.dump(2);
  if (payload.size() > maximum_settings_bytes)
    throw std::runtime_error("video settings payload exceeds 64 KiB");
  const auto bytes = std::span{reinterpret_cast<const std::byte *>(payload.data()),
                               payload.size()};
  stellar::engine::write_file_atomically(path, bytes);
}

VideoSettingsLayout
VideoSettingsLayout::for_viewport(const int width, const int height) {
  const auto scale = std::clamp(static_cast<float>(height) / 720.f, .75f, 2.6f);
  const auto w = static_cast<float>(width), h = static_cast<float>(height);
  VideoSettingsLayout layout;
  layout.scale = scale;
  layout.title_font_pixels = static_cast<int>(std::lround(26.f * scale));
  layout.body_font_pixels = static_cast<int>(std::lround(15.f * scale));
  layout.small_font_pixels = static_cast<int>(std::lround(12.f * scale));
  const auto panel_width = std::min(610.f * scale, w - 24.f * scale);
  const auto panel_height = std::min(400.f * scale, h - 24.f * scale);
  layout.panel = {(w - panel_width) * .5f, (h - panel_height) * .5f,
                  panel_width, panel_height};
  const auto inset = 26.f * scale;
  const auto inner = layout.panel.x + inset;
  const auto inner_width = panel_width - inset * 2.f;
  layout.title = {inner, layout.panel.y + 16.f * scale, inner_width,
                  30.f * scale};
  layout.hint = {inner, layout.title.y + layout.title.height, inner_width,
                 28.f * scale};
  layout.adapter = {inner, layout.hint.y + layout.hint.height, inner_width,
                    20.f * scale};
  auto row_y = layout.adapter.y + layout.adapter.height + 12.f * scale;
  const auto choice_height = 33.f * scale;
  const auto label_width = std::min(150.f * scale, inner_width * .42f);
  const auto choice_width = std::min(250.f * scale, inner_width * .54f);
  for (int index = 0; index < 4; ++index) {
    layout.choice_labels.push_back(
        {inner, row_y + 5.f * scale, label_width, 26.f * scale});
    const UiRect choice_rect{inner + inner_width - choice_width, row_y,
                        choice_width, choice_height - 4.f * scale};
    layout.choice_buttons.push_back(choice_rect);
    const auto chevron_width = std::max(18.f * scale, choice_rect.width * .18f);
    layout.choice_previous.push_back(
        {choice_rect.x, choice_rect.y, choice_rect.width * .25f, choice_rect.height});
    layout.choice_next.push_back(
        {choice_rect.x + choice_rect.width - chevron_width, choice_rect.y, chevron_width,
         choice_rect.height});
    row_y += choice_height;
  }
  layout.error = {inner, row_y + 2.f * scale, inner_width, 26.f * scale};
  const auto actions_y = layout.panel.y + panel_height - 56.f * scale;
  const auto button_height = 38.f * scale;
  layout.apply = {inner + inner_width - 230.f * scale, actions_y,
                  110.f * scale, button_height};
  layout.cancel = {inner + inner_width - 110.f * scale, actions_y,
                   110.f * scale, button_height};
  const auto confirm_width = std::min(510.f * scale, w - 24.f * scale);
  const auto confirm_height = 190.f * scale;
  layout.confirm_panel = {(w - confirm_width) * .5f,
                          (h - confirm_height) * .5f, confirm_width,
                          confirm_height};
  const auto confirm_inner = layout.confirm_panel.x + 24.f * scale;
  const auto confirm_width_inner = confirm_width - 48.f * scale;
  layout.confirm_title = {confirm_inner, layout.confirm_panel.y + 18.f * scale,
                          confirm_width_inner, 30.f * scale};
  layout.confirm_text = {confirm_inner,
                         layout.confirm_title.y + layout.confirm_title.height,
                         confirm_width_inner, 52.f * scale};
  const auto confirm_actions_y =
      layout.confirm_panel.y + confirm_height - 56.f * scale;
  layout.keep = {confirm_inner, confirm_actions_y, 120.f * scale,
                 button_height};
  layout.revert = {confirm_inner + 132.f * scale, confirm_actions_y,
                   120.f * scale, button_height};
  return layout;
}

void NativeVideoSettingsView::open(const NativeVideoSettings current) noexcept {
  values_ = current.sanitized();
  reconcile_resolution();
  visible_ = true;
  confirming_ = false;
}
void NativeVideoSettingsView::close() noexcept {
  visible_ = false;
  confirming_ = false;
}
void NativeVideoSettingsView::set_display_choices(
    std::vector<VideoDisplayChoice> choices, std::string actual_display_label) {
  choices.erase(std::remove_if(choices.begin(), choices.end(),
                               [](const auto &choice) { return !choice.valid(); }),
                choices.end());
  std::sort(choices.begin(), choices.end(), [](const auto &left, const auto &right) {
    return std::tie(left.width, left.height, left.refresh_hz) <
           std::tie(right.width, right.height, right.refresh_hz);
  });
  choices.erase(std::unique(choices.begin(), choices.end()), choices.end());
  display_choices_ = std::move(choices);
  actual_display_label_ = actual_display_label.empty() ? "Desktop default" : std::move(actual_display_label);
  reconcile_resolution();
}
void NativeVideoSettingsView::set_error(std::string message) {
  error_ = std::move(message);
}

void NativeVideoSettingsView::reconcile_resolution() noexcept {
  if (values_.display != VideoDisplayMode::Exclusive) {
    values_.width = values_.height = 0;
    values_.refresh_hz = 0.f;
    return;
  }
  const VideoDisplayChoice selected{values_.width, values_.height, values_.refresh_hz};
  if (!selected.valid() || (!display_choices_.empty() &&
                            std::ranges::find(display_choices_, selected) == display_choices_.end()))
    values_.width = values_.height = 0, values_.refresh_hz = 0.f;
}

void NativeVideoSettingsView::cycle_choice(const int index,
                                           const int direction) noexcept {
  const auto cycle = [direction](auto &value, const int count) {
    value = static_cast<std::decay_t<decltype(value)>>(
        (static_cast<int>(value) + direction + count) % count);
  };
  if (index == 0) {
    cycle(values_.display, 2);
    reconcile_resolution();
  } else if (index == 1 && values_.display == VideoDisplayMode::Exclusive &&
             !display_choices_.empty()) {
    const VideoDisplayChoice selected{values_.width, values_.height, values_.refresh_hz};
    const auto found = std::ranges::find(display_choices_, selected);
    if (found == display_choices_.end()) {
      const auto &next = direction < 0 ? display_choices_.back()
                                       : display_choices_.front();
      values_.width = next.width;
      values_.height = next.height;
      values_.refresh_hz = next.refresh_hz;
    } else if (direction < 0 && found == display_choices_.begin()) {
      values_.width = values_.height = 0;
      values_.refresh_hz = 0.f;
    } else if (direction > 0 && std::next(found) == display_choices_.end()) {
      values_.width = values_.height = 0;
      values_.refresh_hz = 0.f;
    } else {
      const auto next = direction < 0 ? std::prev(found) : std::next(found);
      values_.width = next->width;
      values_.height = next->height;
      values_.refresh_hz = next->refresh_hz;
    }
  } else if (index == 2) cycle(values_.vsync, 3);
  else if (index == 3) cycle(values_.frame_cap, 5);
}

VideoSettingsResult
NativeVideoSettingsView::handle(const InputEvent &event, const int width,
                                const int height) {
  VideoSettingsResult result{};
  if (!visible_) return result;
  result.captured = true;
  result.values = values_;
  pointer_ = event.position;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);

  if (confirming_) {
    if (event.type == InputEventType::EscapePressed ||
        event.type == InputEventType::PointerCancelled) {
      result.command = VideoSettingsCommand::Revert;
      return result;
    }
    if (event.type == InputEventType::LeftPressed) {
      if (layout.keep.contains(event.position))
        result.command = VideoSettingsCommand::Keep;
      else if (layout.revert.contains(event.position))
        result.command = VideoSettingsCommand::Revert;
    }
    return result;
  }

  if (event.type == InputEventType::EscapePressed) {
    result.command = VideoSettingsCommand::Cancel;
    return result;
  }
  if (event.type == InputEventType::LeftPressed) {
    if (layout.apply.contains(event.position)) {
      result.command = VideoSettingsCommand::Apply;
      return result;
    }
    if (layout.cancel.contains(event.position)) {
      result.command = VideoSettingsCommand::Cancel;
      return result;
    }
    for (int index = 0; index < 4; ++index) {
      if (!layout.choice_buttons[index].contains(event.position)) continue;
      const auto direction = layout.choice_previous[index].contains(event.position)
                                 ? -1
                                 : 1;
      cycle_choice(index, direction);
      result.values = values_;
      return result;
    }
    return result;
  }
  return result;
}

void NativeVideoSettingsView::render(DrawList &out, const int width,
                                     const int height,
                                     const double rollback_remaining) const {
  if (!visible_) return;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  fill(out, {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
       {4, 9, 18, 160});
  fill(out, layout.panel, panel);
  stroke(out, layout.panel, border);
  text(out, layout.title, "VIDEO", text_primary,
       layout.title_font_pixels, TextAlign::Left, FontFace::Heading);
  text(out, layout.hint,
       "Use the left side for previous; the remaining area selects next.",
       text_muted, layout.small_font_pixels);
  const auto draw_button = [&](UiRect bounds, std::string caption, bool enabled = true, float inset = 0.f) {
    fill(out, bounds, enabled && bounds.contains(pointer_) ? hover : button);
    stroke(out, bounds, enabled ? border : text_muted);
    text(out, {bounds.x + inset, bounds.y + bounds.height * .5f - layout.body_font_pixels * .55f,
               bounds.width - inset * 2.f, static_cast<float>(layout.body_font_pixels) * 1.2f},
         std::move(caption), enabled ? text_primary : text_muted, layout.body_font_pixels,
         TextAlign::Center);
  };
  const std::array<std::string, 4> choice_values = {
      std::string(display_name(values_.display)),
      resolution_name(values_, actual_display_label_), std::string(vsync_name(values_.vsync)),
      std::string(frame_cap_name(values_.frame_cap))};
  for (int index = 0; index < 4; ++index) {
    text(out, layout.choice_labels[index],
         std::string(choice_names[static_cast<std::size_t>(index)]), gold,
         layout.small_font_pixels);
    const auto enabled = index != 1 ||
                         (values_.display == VideoDisplayMode::Exclusive && !display_choices_.empty());
    const auto bounds=layout.choice_buttons[index];
    draw_button(bounds, choice_values[static_cast<std::size_t>(index)],enabled,enabled?22.f*layout.scale:0.f);
    if(enabled){
      const auto y=bounds.y + bounds.height*.5f - layout.small_font_pixels*.55f;
      text(out,{bounds.x+2.f*layout.scale,y,20.f*layout.scale,static_cast<float>(layout.small_font_pixels)*1.2f},
           "<",text_primary,layout.small_font_pixels,TextAlign::Center);
      text(out,{bounds.x+bounds.width-22.f*layout.scale,y,20.f*layout.scale,static_cast<float>(layout.small_font_pixels)*1.2f},
           ">",text_primary,layout.small_font_pixels,TextAlign::Center);
    }
  }
  if (!error_.empty())
    text(out, layout.error, error_, text_error,
         layout.small_font_pixels);
  draw_button(layout.apply, "APPLY");
  draw_button(layout.cancel, "CANCEL");

  if (confirming_) {
    fill(out,
         {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
         {0, 0, 0, 184});
    fill(out, layout.confirm_panel, panel);
    stroke(out, layout.confirm_panel, border);
    text(out, layout.confirm_title,
         "CONFIRM DISPLAY", text_primary, layout.title_font_pixels,
         TextAlign::Left, FontFace::Heading);
    text(out, layout.confirm_text,
         "Keep these display settings? Reverting in " +
             std::to_string(
                 std::max(0, static_cast<int>(std::ceil(rollback_remaining)))) +
             " seconds.",
         text_muted, layout.body_font_pixels);
    draw_button(layout.keep, "KEEP");
    draw_button(layout.revert, "REVERT");
  }
}

} // namespace stellar::native_video_settings
