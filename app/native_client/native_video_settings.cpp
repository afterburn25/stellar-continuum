#include "native_video_settings.hpp"
#include "native_menu_style.hpp"

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
constexpr std::array<std::string_view, 8> choice_names = {"DISPLAY", "RESOLUTION", "V-SYNC",
                                                        "FRAME CAP", "EDGE SMOOTHING", "SCENE RESOLUTION", "STARFIELD QUALITY", "STARFIELD DENSITY"};
constexpr std::array<std::string_view, 3> display_names = {"Borderless fullscreen",
                                                         "Exclusive fullscreen", "Windowed"};
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
  return value == VideoDisplayMode::Exclusive ? "Exclusive" :
         value == VideoDisplayMode::Windowed ? "Windowed" : "Borderless";
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
  if (value == "Windowed") return VideoDisplayMode::Windowed;
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
  if (value.display == VideoDisplayMode::Borderless)
    return "Desktop (" + std::string(desktop) + ")";
  if (value.display == VideoDisplayMode::Windowed) {
    if (value.width <= 0 || value.height <= 0) return "Default window size";
    return std::to_string(value.width) + " x " + std::to_string(value.height);
  }
  if (value.width <= 0 || value.height <= 0 || !std::isfinite(value.refresh_hz) || value.refresh_hz <= 0.f)
    return "Desktop default";
  return std::to_string(value.width) + " x " + std::to_string(value.height) + " @ " +
         std::to_string(static_cast<int>(std::lround(value.refresh_hz))) + " Hz";
}
} // namespace

NativeVideoSettings NativeVideoSettings::sanitized() const noexcept {
  auto copy = *this;
  if (copy.display != VideoDisplayMode::Borderless &&
      copy.display != VideoDisplayMode::Exclusive &&
      copy.display != VideoDisplayMode::Windowed)
    copy.display = VideoDisplayMode::Borderless;
  if (copy.vsync != VideoVsync::Off && copy.vsync != VideoVsync::On &&
      copy.vsync != VideoVsync::Adaptive)
    copy.vsync = VideoVsync::On;
  if (enum_index(copy.frame_cap, 5) == 0 &&
      copy.frame_cap != VideoFrameCap::Automatic)
    copy.frame_cap = VideoFrameCap::Automatic;
  if (copy.width < 0 || copy.width > 16384 || copy.height < 0 || copy.height > 16384 ||
      !std::isfinite(copy.refresh_hz) || copy.refresh_hz < 0.f || copy.refresh_hz > 1000.f ||
      (copy.width == 0) != (copy.height == 0) ||
      (copy.display != VideoDisplayMode::Windowed &&
       ((copy.width == 0) != (copy.refresh_hz == 0.f))))
    copy.width = copy.height = 0, copy.refresh_hz = 0.f;
  if(copy.display == VideoDisplayMode::Windowed)copy.refresh_hz=0.f;
  if(copy.scene_resolution_percent!=50&&copy.scene_resolution_percent!=75&&copy.scene_resolution_percent!=100)copy.scene_resolution_percent=100;
  if(copy.scene_samples!=1&&copy.scene_samples!=2&&copy.scene_samples!=4)copy.scene_samples=1;
  if(copy.starfield_quality<0||copy.starfield_quality>3)copy.starfield_quality=2;
  if(copy.starfield_density<0||copy.starfield_density>2)copy.starfield_density=1;
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
    settings.scene_resolution_percent=read_dimension(document,"sceneResolutionPercent");
    settings.scene_samples=read_dimension(document,"sceneSamples");
    if(document.contains("starfieldQuality")&&document["starfieldQuality"].is_number_integer())settings.starfield_quality=document["starfieldQuality"].get<int>();
    if(document.contains("starfieldDensity")&&document["starfieldDensity"].is_number_integer())settings.starfield_density=document["starfieldDensity"].get<int>();
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
  document["sceneResolutionPercent"]=settings.scene_resolution_percent;
  document["sceneSamples"]=settings.scene_samples;
  document["starfieldQuality"]=settings.starfield_quality;
  document["starfieldDensity"]=settings.starfield_density;
  const auto payload = document.dump(2);
  if (payload.size() > maximum_settings_bytes)
    throw std::runtime_error("video settings payload exceeds 64 KiB");
  const auto bytes = std::span{reinterpret_cast<const std::byte *>(payload.data()),
                               payload.size()};
  stellar::engine::write_file_atomically(path, bytes);
}

VideoSettingsLayout
VideoSettingsLayout::for_viewport(const int width, const int height) {
  const auto w = static_cast<float>(width), h = static_cast<float>(height);
  const auto scale = std::max(.01f, std::min({std::clamp(h / 1080.f, .8f, 2.5f),
                                            (w - 24.f) / 760.f, (h - 24.f) / 730.f}));
  VideoSettingsLayout layout;
  layout.scale = scale;
  layout.title_font_pixels = static_cast<int>(std::lround(28.f * scale));
  layout.body_font_pixels = static_cast<int>(std::lround(18.f * scale));
  layout.small_font_pixels = static_cast<int>(std::lround(15.f * scale));
  const auto panel_width = std::min(760.f * scale, w - 24.f * scale);
  const auto panel_height = std::min(730.f * scale, h - 24.f * scale);
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
  const auto choice_height = 49.f * scale;
  const auto label_width = std::min(200.f * scale, inner_width * .30f);
  const auto choice_width = std::min(460.f * scale, inner_width * .68f);
  for (int index = 0; index < 8; ++index) {
    layout.choice_labels.push_back(
        {inner, row_y + 5.f * scale, label_width, 26.f * scale});
    const UiRect choice_rect{inner + inner_width - choice_width, row_y,
                        choice_width, choice_height - 4.f * scale};
    layout.choice_buttons.push_back(choice_rect);
    row_y += choice_height;
  }
  layout.quality_hint={inner,row_y+6*scale,inner_width,42*scale};
  layout.nvidia={inner,row_y+58*scale,inner_width,38*scale};
  layout.error = {inner, row_y + 102.f * scale, inner_width, 30.f * scale};
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
  hover_feedback_.reset();
  dropdown_.close();
  values_ = current.sanitized();
  reconcile_resolution();
  visible_ = true;
  confirming_ = false;
  focus_ = -1;
}
void NativeVideoSettingsView::close() noexcept {
  dropdown_.close();
  visible_ = false;
  confirming_ = false;
  focus_ = -1;
}
int NativeVideoSettingsView::collect_focusables(
    const VideoSettingsLayout &layout, std::array<Focusable, 11> &out) const {
  int count = 0;
  if (confirming_) {
    out[count++] = {layout.keep, -4, 1};
    out[count++] = {layout.revert, -5, 2};
    return count;
  }
  for (std::size_t i = 0; i < layout.choice_buttons.size(); ++i) {
    if (i == 1 && values_.display == VideoDisplayMode::Borderless) continue;
    out[count++] = {layout.choice_buttons[i], static_cast<int>(i), 10 + i};
  }
  if (open_panel_) out[count++] = {layout.nvidia, -1, 3};
  out[count++] = {layout.apply, -2, 1};
  out[count++] = {layout.cancel, -3, 2};
  return count;
}
std::optional<stellar::native_map::UiRect>
NativeVideoSettingsView::focused_bounds(int width, int height) const {
  if (!visible_ || focus_ < 0) return std::nullopt;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  std::array<Focusable, 11> focusables{};
  const int count = collect_focusables(layout, focusables);
  return focus_ < count
             ? std::optional<stellar::native_map::UiRect>{
                   focusables[static_cast<std::size_t>(focus_)].rect}
             : std::nullopt;
}
std::string NativeVideoSettingsView::focused_label(int width, int height) const {
  if (!visible_ || focus_ < 0) return {};
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  std::array<Focusable, 11> focusables{};
  const int count = collect_focusables(layout, focusables);
  if (focus_ >= count) return {};
  const int target = focusables[static_cast<std::size_t>(focus_)].target;
  switch (target) {
  case -1: return tr("SETTINGS_VIDEO_OPEN_NVIDIA", "Open NVIDIA Control Panel");
  case -2: return tr("SETTINGS_VIDEO_APPLY", "Apply");
  case -3: return tr("SETTINGS_CANCEL", "Cancel");
  case -4: return tr("SETTINGS_VIDEO_KEEP", "Keep");
  case -5: return tr("SETTINGS_VIDEO_REVERT", "Revert");
  default: break;
  }
  if (target < 0 || target >= 8) return {};
  const std::array<std::string, 8> choice_values = {
      std::string(display_name(values_.display)),
      resolution_name(values_, actual_display_label_), std::string(vsync_name(values_.vsync)),
      std::string(frame_cap_name(values_.frame_cap))+(values_.frame_cap==VideoFrameCap::Automatic?" · "+actual_display_label_:""),
      values_.scene_samples==1?"Off":std::to_string(values_.scene_samples)+"x supersampling",
      std::to_string(values_.scene_resolution_percent)+"%"+(values_.scene_resolution_percent==100?" · Native":" · Reduced"),
      std::array<std::string,4>{"Low","Medium","High","Ultra"}[values_.starfield_quality],
      std::array<std::string,3>{"Low","Normal","High"}[values_.starfield_density]};
  const std::array<std::string_view, 8> choice_keys = {
      "SETTINGS_VIDEO_DISPLAY", "SETTINGS_VIDEO_RESOLUTION", "SETTINGS_VIDEO_VSYNC",
      "SETTINGS_VIDEO_FRAME_CAP", "SETTINGS_VIDEO_SMOOTHING", "SETTINGS_VIDEO_SCENE_RES",
      "SETTINGS_VIDEO_STARFIELD_QUALITY", "SETTINGS_VIDEO_STARFIELD_DENSITY"};
  const auto index = static_cast<std::size_t>(target);
  return tr(choice_keys[index], choice_names[index]) + ": " + choice_values[index];
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
  windowed_choices_=display_choices_;
  windowed_choices_.erase(std::unique(windowed_choices_.begin(),windowed_choices_.end(),
    [](const auto& left,const auto& right){return left.width==right.width&&left.height==right.height;}),windowed_choices_.end());
  actual_display_label_ = actual_display_label.empty() ? "Desktop default" : std::move(actual_display_label);
  reconcile_resolution();
}
void NativeVideoSettingsView::set_windowed_choices(std::vector<VideoDisplayChoice> choices){
  choices.erase(std::remove_if(choices.begin(),choices.end(),[](const auto& choice){return choice.width<=0||choice.width>16384||choice.height<=0||choice.height>16384;}),choices.end());
  std::sort(choices.begin(),choices.end(),[](const auto& left,const auto& right){return std::tie(left.width,left.height,left.refresh_hz)<std::tie(right.width,right.height,right.refresh_hz);});
  choices.erase(std::unique(choices.begin(),choices.end(),[](const auto& left,const auto& right){return left.width==right.width&&left.height==right.height;}),choices.end());
  windowed_choices_=std::move(choices);reconcile_resolution();
}
void NativeVideoSettingsView::set_error(std::string message) {
  error_ = std::move(message);
}

void NativeVideoSettingsView::reconcile_resolution() noexcept {
  if (values_.display == VideoDisplayMode::Borderless) {
    values_.width = values_.height = 0;
    values_.refresh_hz = 0.f;
    return;
  }
  if(values_.display == VideoDisplayMode::Windowed){
    values_.refresh_hz=0.f;
    if(values_.width==0&&values_.height==0){
      if(!windowed_choices_.empty()){values_.width=windowed_choices_.front().width;values_.height=windowed_choices_.front().height;}
      return;
    }
    const auto found=std::ranges::find_if(windowed_choices_,[&](const auto& choice){return choice.width==values_.width&&choice.height==values_.height;});
    if(found==windowed_choices_.end())values_.width=values_.height=0;
    return;
  }
  const VideoDisplayChoice selected{values_.width, values_.height, values_.refresh_hz};
  if (!selected.valid() || (!display_choices_.empty() &&
                            std::ranges::find(display_choices_, selected) == display_choices_.end()))
    values_.width = values_.height = 0, values_.refresh_hz = 0.f;
}

void NativeVideoSettingsView::open_choice(int index) {
  std::vector<std::string> options;
  int selected{};
  if(index==0){for(const auto name:display_names)options.emplace_back(name);selected=static_cast<int>(values_.display);}
  else if(index==1){
    if(values_.display==VideoDisplayMode::Borderless)return;
    const bool windowed=values_.display==VideoDisplayMode::Windowed;
    options.emplace_back(windowed?"Default window size":"Desktop default");
    const auto& choices=windowed?windowed_choices_:display_choices_;
    for(const auto& choice:choices){
      auto value=values_;value.width=choice.width;value.height=choice.height;value.refresh_hz=choice.refresh_hz;
      options.push_back(resolution_name(value,actual_display_label_));
      if(values_.width==choice.width&&values_.height==choice.height&&(windowed||values_.refresh_hz==choice.refresh_hz))selected=static_cast<int>(options.size())-1;
    }
  }else if(index==2){for(const auto name:vsync_names)options.emplace_back(name);selected=static_cast<int>(values_.vsync);}
  else if(index==3){for(const auto name:frame_cap_names)options.emplace_back(name);selected=static_cast<int>(values_.frame_cap);}
  else if(index==4){options={"Off","2x supersampling","4x supersampling"};selected=values_.scene_samples==1?0:values_.scene_samples==2?1:2;}
  else if(index==5){options={"50% · Reduced","75% · Reduced","100% · Native"};selected=(values_.scene_resolution_percent-50)/25;}
  else if(index==6){options={"Low","Medium","High","Ultra"};selected=values_.starfield_quality;}
  else if(index==7){options={"Low","Normal","High"};selected=values_.starfield_density;}
  dropdown_.open(index,std::move(options),selected);
}

void NativeVideoSettingsView::select_choice(int index,int option) noexcept {
  if(index==0){values_.display=static_cast<VideoDisplayMode>(option);reconcile_resolution();}
  else if(index==1){
    if(option==0){values_.width=values_.height=0;values_.refresh_hz=0.f;}
    else {const bool windowed=values_.display==VideoDisplayMode::Windowed;const auto& choices=windowed?windowed_choices_:display_choices_;
      if(option<=static_cast<int>(choices.size())){const auto& choice=choices[option-1];values_.width=choice.width;values_.height=choice.height;values_.refresh_hz=windowed?0.f:choice.refresh_hz;}}
  }else if(index==2)values_.vsync=static_cast<VideoVsync>(option);
  else if(index==3)values_.frame_cap=static_cast<VideoFrameCap>(option);
  else if(index==4)values_.scene_samples=option==0?1:option==1?2:4;
  else if(index==5)values_.scene_resolution_percent=50+option*25;
  else if(index==6)values_.starfield_quality=option;
  else if(index==7)values_.starfield_density=option;
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
  if(dropdown_.visible()&&!confirming_){
    const int index=dropdown_.id();const auto anchor=layout.choice_buttons[index];
    hover_feedback_.update(event,dropdown_.hover_target(event.position,anchor,width,height));
    if(const auto selected=dropdown_.handle(event,anchor,width,height))select_choice(index,*selected);
    result.values=values_;return result;
  }
  auto target=confirming_?stellar::native_menu_audio::hit(event.position,{layout.keep,layout.revert}):stellar::native_menu_audio::hit(event.position,{layout.apply,layout.cancel,open_panel_?layout.nvidia:stellar::native_map::UiRect{}});
  if(!confirming_)for(std::size_t i=0;i<layout.choice_buttons.size();++i){
    if(i==1&&values_.display==VideoDisplayMode::Borderless)continue;
    if(layout.choice_buttons[i].contains(event.position))target=10+i;
  }
  hover_feedback_.update(event,target);

  if (event.type == InputEventType::LeftPressed) focus_ = -1;
  if (event.type == InputEventType::KeyPressed) {
    // SDL_Keycode: Tab/arrows ring the live controls, Home/End jump to
    // the ends, Return/Space activate through the same commands/clicks.
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                            kDown = 0x40000051u, kUp = 0x40000052u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    std::array<Focusable, 11> focusables{};
    const int count = collect_focusables(layout, focusables);
    if (event.key == kHome || event.key == kEnd) {
      focus_ = event.key == kHome ? 0 : count - 1;
      hover_feedback_.cue(focusables[focus_].cue);
      return result;
    }
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    if (fwd || bwd) {
      if (focus_ < 0) focus_ = bwd ? count - 1 : 0;
      else focus_ = (focus_ + (bwd ? -1 : 1) + count) % count;
      if (focus_ >= count) focus_ = 0;
      hover_feedback_.cue(focusables[focus_].cue);
      return result;
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 && focus_ < count) {
      switch (focusables[focus_].target) {
        case -1: try { open_panel_(); } catch (const std::exception &e) { set_error(e.what()); } break;
        case -2: result.command = VideoSettingsCommand::Apply; break;
        case -3: result.command = VideoSettingsCommand::Cancel; break;
        case -4: result.command = VideoSettingsCommand::Keep; break;
        case -5: result.command = VideoSettingsCommand::Revert; break;
        default: open_choice(focusables[focus_].target); break;
      }
      return result;
    }
    return result;
  }

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
    if (layout.nvidia.contains(event.position) && open_panel_) {try{open_panel_();}catch(const std::exception& e){set_error(e.what());}return result;}
    if (layout.apply.contains(event.position)) {
      result.command = VideoSettingsCommand::Apply;
      return result;
    }
    if (layout.cancel.contains(event.position)) {
      result.command = VideoSettingsCommand::Cancel;
      return result;
    }
    for (int index = 0; index < 8; ++index) {
      if (!layout.choice_buttons[index].contains(event.position)) continue;
      open_choice(index);
      return result;
    }
    return result;
  }
  return result;
}

std::string NativeVideoSettingsView::tr(std::string_view key,
                                        std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string NativeVideoSettingsView::trf(std::string_view key,
                                         std::string_view arg,
                                         std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) {
    const std::string value{arg};
    return locale_->format(key, std::span<const std::string>{&value, 1});
  }
  std::string out{fallback};
  if (const auto at = out.find("{0}"); at != std::string::npos)
    out.replace(at, 3, arg);
  return out;
}

void NativeVideoSettingsView::render(DrawList &out, const int width,
                                     const int height,
                                     const double rollback_remaining) const {
  if (!visible_) return;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  fill(out, {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
       {4, 9, 18, 48});
  native_menu_style::panel(out,layout.panel,layout.scale);
  text(out, layout.title, tr("SETTINGS_NAV_VIDEO", "VIDEO"), text_primary,
       layout.title_font_pixels, TextAlign::Left, FontFace::Heading);
  text(out, layout.hint,
       tr("SETTINGS_VIDEO_HINT",
          "Detected display modes and graphics controls for this computer."),
       text_muted, layout.small_font_pixels);
  text(out,layout.adapter,adapter_label_,{112,223,238,255},layout.small_font_pixels);
  const auto draw_button = [&](UiRect bounds, std::string caption, bool enabled = true, float inset = 0.f) {
    stellar::engine::ui_skin::control(out,bounds,bounds.contains(pointer_),false,enabled,layout.scale);
    text(out, {bounds.x + inset, bounds.y + bounds.height * .5f - layout.body_font_pixels * .55f,
               bounds.width - inset * 2.f, static_cast<float>(layout.body_font_pixels) * 1.2f},
         std::move(caption), enabled ? text_primary : text_muted, layout.body_font_pixels,
         TextAlign::Center);
  };
  const std::array<std::string, 8> choice_values = {
      std::string(display_name(values_.display)),
      resolution_name(values_, actual_display_label_), std::string(vsync_name(values_.vsync)),
      std::string(frame_cap_name(values_.frame_cap))+(values_.frame_cap==VideoFrameCap::Automatic?" · "+actual_display_label_:""),
      values_.scene_samples==1?"Off":std::to_string(values_.scene_samples)+"x supersampling",
      std::to_string(values_.scene_resolution_percent)+"%"+(values_.scene_resolution_percent==100?" · Native":" · Reduced"),
      std::array<std::string,4>{"Low","Medium","High","Ultra"}[values_.starfield_quality],
      std::array<std::string,3>{"Low","Normal","High"}[values_.starfield_density]};
  const std::array<std::string_view, 8> choice_keys = {
      "SETTINGS_VIDEO_DISPLAY", "SETTINGS_VIDEO_RESOLUTION", "SETTINGS_VIDEO_VSYNC",
      "SETTINGS_VIDEO_FRAME_CAP", "SETTINGS_VIDEO_SMOOTHING", "SETTINGS_VIDEO_SCENE_RES",
      "SETTINGS_VIDEO_STARFIELD_QUALITY", "SETTINGS_VIDEO_STARFIELD_DENSITY"};
  for (int index = 0; index < 8; ++index) {
    text(out, layout.choice_labels[index],
         tr(choice_keys[static_cast<std::size_t>(index)],
            choice_names[static_cast<std::size_t>(index)]),
         gold, layout.small_font_pixels);
    const auto enabled = index != 1 ||
                         ((values_.display == VideoDisplayMode::Exclusive && !display_choices_.empty()) ||
                          (values_.display == VideoDisplayMode::Windowed && !windowed_choices_.empty()));
    const auto bounds=layout.choice_buttons[index];
    draw_button(bounds, choice_values[static_cast<std::size_t>(index)],enabled,enabled?22.f*layout.scale:0.f);
    if(enabled){
      const auto y=bounds.y+bounds.height*.5f-layout.small_font_pixels*.55f;
      text(out,{bounds.x+bounds.width-24.f*layout.scale,y,20.f*layout.scale,static_cast<float>(layout.small_font_pixels)*1.2f},
           "▼",text_primary,layout.small_font_pixels,TextAlign::Center);
    }
  }
  text(out,layout.quality_hint,tr("SETTINGS_VIDEO_QUALITY_HINT","Borderless fullscreen is recommended. Supersampling smooths the scene; interface text stays at native resolution."),text_muted,layout.small_font_pixels);
  if(open_panel_)draw_button(layout.nvidia,tr("SETTINGS_VIDEO_OPEN_NVIDIA","Open NVIDIA Control Panel"));
  else text(out,layout.nvidia,tr("SETTINGS_VIDEO_NO_NVIDIA","NVIDIA Control Panel is not installed on this computer."),text_muted,layout.small_font_pixels);
  if (!error_.empty())
    text(out, layout.error, error_, text_error,
         layout.small_font_pixels);
  draw_button(layout.apply, tr("SETTINGS_VIDEO_APPLY", "APPLY"));
  draw_button(layout.cancel, tr("SETTINGS_CANCEL", "CANCEL"));

  if(dropdown_.visible()&&!confirming_)dropdown_.render(out,layout.choice_buttons[dropdown_.id()],width,height,layout.body_font_pixels);

  if (confirming_) {
    fill(out,
         {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
         {0, 0, 0, 184});
    stellar::engine::ui_skin::surface(out,layout.confirm_panel,layout.scale);
    text(out, layout.confirm_title,
         tr("SETTINGS_VIDEO_CONFIRM_TITLE", "CONFIRM DISPLAY"), text_primary,
         layout.title_font_pixels, TextAlign::Left, FontFace::Heading);
    text(out, layout.confirm_text,
         trf("SETTINGS_VIDEO_CONFIRM_TEXT",
             std::to_string(
                 std::max(0, static_cast<int>(std::ceil(rollback_remaining)))),
             "Keep these display settings? Reverting in {0} seconds."),
         text_muted, layout.body_font_pixels);
    draw_button(layout.keep, tr("SETTINGS_VIDEO_KEEP", "KEEP"));
    draw_button(layout.revert, tr("SETTINGS_VIDEO_REVERT", "REVERT"));
  }
  if (focus_ >= 0) {
    std::array<Focusable, 11> focusables{};
    const int count = collect_focusables(layout, focusables);
    if (focus_ < count)
      out.overlay.emplace_back(stellar::native_map::StrokedRectangle{
          focusables[focus_].rect, {160, 210, 255, 255}});
  }
}

} // namespace stellar::native_video_settings
