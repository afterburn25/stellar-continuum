#include "native_video_settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

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

constexpr std::array<std::string_view, 3> choice_names = {"DISPLAY", "V-SYNC",
                                                        "FRAME CAP"};
constexpr std::array<std::string_view, 2> display_names = {"Fullscreen",
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
void text(DrawList &out, Point at, std::string value, Color color, int pixels,
          TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface) {
  out.overlay.emplace_back(Text{at, std::move(value), color, pixels, 0.f,
                                std::nullopt, align, face});
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
  return copy;
}

NativeVideoSettings
NativeVideoSettings::load(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return {};
  try {
    std::ifstream input(path, std::ios::binary);
    const auto document = nlohmann::json::parse(input, nullptr, true, true);
    NativeVideoSettings settings;
    if (const auto display = parse_display(read_string(document, "display")))
      settings.display = *display;
    if (const auto vsync = parse_vsync(read_string(document, "vsync")))
      settings.vsync = *vsync;
    if (const auto cap = parse_frame_cap(read_string(document, "frameCap")))
      settings.frame_cap = *cap;
    return settings.sanitized();
  } catch (const std::exception &) {
    return {};
  }
}

void NativeVideoSettings::save(const std::filesystem::path &path) const {
  const auto settings = sanitized();
  std::error_code error;
  const auto directory = path.parent_path();
  if (!directory.empty()) std::filesystem::create_directories(directory, error);
  const auto tmp = path.parent_path() / (path.filename().string() + ".tmp");
  nlohmann::json document;
  document["display"] = display_key(settings.display);
  document["vsync"] = vsync_key(settings.vsync);
  document["frameCap"] = frame_cap_key(settings.frame_cap);
  {
    std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
    output << document.dump(2);
  }
  std::filesystem::rename(tmp, path, error);
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
  const auto panel_height = std::min(330.f * scale, h - 24.f * scale);
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
  const auto choice_height = 34.f * scale;
  const auto label_width = 190.f * scale;
  const auto choice_width = 230.f * scale;
  for (int index = 0; index < 3; ++index) {
    layout.choice_labels.push_back(
        {inner, row_y + 5.f * scale, label_width, 26.f * scale});
    layout.choice_buttons.push_back(
        {inner + inner_width - choice_width, row_y, choice_width,
         choice_height - 4.f * scale});
    row_y += choice_height;
  }
  layout.error = {inner, row_y + 2.f * scale, inner_width, 20.f * scale};
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
  visible_ = true;
  confirming_ = false;
  error_.clear();
}
void NativeVideoSettingsView::close() noexcept {
  visible_ = false;
  confirming_ = false;
}
void NativeVideoSettingsView::set_error(std::string message) {
  error_ = std::move(message);
}

void NativeVideoSettingsView::cycle_choice(const int index) noexcept {
  const auto cycle = [](auto &value, const int count) {
    value = static_cast<std::decay_t<decltype(value)>>(
        (static_cast<int>(value) + 1) % count);
  };
  if (index == 0) cycle(values_.display, 2);
  else if (index == 1) cycle(values_.vsync, 3);
  else if (index == 2) cycle(values_.frame_cap, 5);
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
    for (int index = 0; index < 3; ++index) {
      if (!layout.choice_buttons[index].contains(event.position)) continue;
      cycle_choice(index);
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
  text(out, {layout.title.x, layout.title.y}, "VIDEO", text_primary,
       layout.title_font_pixels, TextAlign::Left, FontFace::Heading);
  text(out, {layout.hint.x, layout.hint.y},
       "Stellar Continuum always fills your display. Resolution, MSAA and 3D "
       "resolution follow the surface renderer.",
       text_muted, layout.small_font_pixels);
  const auto draw_button = [&](UiRect bounds, std::string caption) {
    fill(out, bounds, bounds.contains(pointer_) ? hover : button);
    stroke(out, bounds, border);
    text(out,
         {bounds.x + bounds.width * .5f,
          bounds.y + bounds.height * .5f - layout.body_font_pixels * .55f},
         std::move(caption), text_primary, layout.body_font_pixels,
         TextAlign::Center);
  };
  const std::array<std::string_view, 3> choice_values = {
      display_name(values_.display), vsync_name(values_.vsync),
      frame_cap_name(values_.frame_cap)};
  for (int index = 0; index < 3; ++index) {
    text(out,
         {layout.choice_labels[index].x, layout.choice_labels[index].y},
         std::string(choice_names[static_cast<std::size_t>(index)]), gold,
         layout.small_font_pixels);
    draw_button(layout.choice_buttons[index],
                std::string(choice_values[static_cast<std::size_t>(index)]));
  }
  if (!error_.empty())
    text(out, {layout.error.x, layout.error.y}, error_, text_error,
         layout.small_font_pixels);
  draw_button(layout.apply, "APPLY");
  draw_button(layout.cancel, "CANCEL");

  if (confirming_) {
    fill(out,
         {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
         {0, 0, 0, 184});
    fill(out, layout.confirm_panel, panel);
    stroke(out, layout.confirm_panel, border);
    text(out, {layout.confirm_title.x, layout.confirm_title.y},
         "CONFIRM DISPLAY", text_primary, layout.title_font_pixels,
         TextAlign::Left, FontFace::Heading);
    text(out, {layout.confirm_text.x, layout.confirm_text.y},
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
