#include "native_video_settings.hpp"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace {
using namespace stellar::native_video_settings;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

InputEvent press(InputEventType type, Point at) {
  InputEvent event{};
  event.type = type;
  event.position = at;
  return event;
}

void responsive_layout() {
  for (const auto [width, height] :
       {std::pair{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}) {
    const auto layout = VideoSettingsLayout::for_viewport(width, height);
    const UiRect viewport{0.f, 0.f, static_cast<float>(width),
                          static_cast<float>(height)};
    const auto inside = [&](const UiRect &rect) {
      return rect.x >= viewport.x && rect.y >= viewport.y &&
             rect.x + rect.width <= viewport.x + viewport.width &&
             rect.y + rect.height <= viewport.y + viewport.height;
    };
    require(inside(layout.panel), "panel escaped");
    require(layout.choice_labels.size() == 6 && layout.choice_buttons.size() == 6 &&
                layout.choice_previous.size() == 6 && layout.choice_next.size() == 6,
            "choice rows incomplete");
    for (int index = 0; index < 4; ++index) {
      require(inside(layout.choice_buttons[index]), "choice escaped");
      require(inside(layout.choice_previous[index]) && inside(layout.choice_next[index]),
              "choice chevrons escaped");
      require(layout.panel.contains({layout.choice_buttons[index].x + 1.f,
                                     layout.choice_buttons[index].y + 1.f}),
              "choice outside panel");
      if (index > 0)
        require(layout.choice_buttons[index].y >
                    layout.choice_buttons[index - 1].y,
                "choice rows overlap");
    }
    require(inside(layout.apply) && inside(layout.cancel),
            "action buttons escaped");
    require(layout.apply.y > layout.choice_buttons.back().y,
            "actions above choices");
    require(inside(layout.confirm_panel) && inside(layout.keep) &&
                inside(layout.revert),
            "confirm overlay escaped");
    NativeVideoSettingsView view;
    view.open({});
    view.set_display_choices({{1920, 1080, 60.f}}, "1920 x 1080 @ 60 Hz");
    DrawList draw;
    view.render(draw, width, height);
    for (const auto &command : draw.overlay) {
      const auto *label = std::get_if<Text>(&command);
      if (!label) continue;
      require(label->clip && inside(*label->clip),
              "video settings emitted text without a bounded clip");
    }
  }
}

void persistence_round_trip() {
  const auto directory =
      std::filesystem::temp_directory_path() / ("stellar-video-settings-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  require(std::filesystem::create_directory(directory, error), "could not own a fresh settings test directory");
  const auto path = directory / "video-settings.json";

  // Missing file loads defaults.
  std::filesystem::remove(path, error);
  const auto defaults = NativeVideoSettings::load(path);
  require(defaults.display == VideoDisplayMode::Borderless &&
              defaults.vsync == VideoVsync::On &&
              defaults.frame_cap == VideoFrameCap::Automatic,
          "defaults differ from NativeVideoSettings");

  NativeVideoSettings saved{};
  saved.display = VideoDisplayMode::Exclusive;
  saved.width = 2560;
  saved.height = 1440;
  saved.refresh_hz = 144.f;
  saved.vsync = VideoVsync::Adaptive;
  saved.frame_cap = VideoFrameCap::Fps144;
  saved.save(path);
  const auto loaded = NativeVideoSettings::load(path);
  require(loaded == saved, "video settings did not round-trip");
  {
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    require(bytes.find("Exclusive") != std::string::npos,
            "exclusive display key changed during persistence");
  }
  NativeVideoSettings windowed{};
  windowed.display = VideoDisplayMode::Windowed;
  windowed.width = 1280;
  windowed.height = 720;
  windowed.refresh_hz = 0.f;
  windowed.save(path);
  require(NativeVideoSettings::load(path) == windowed,
          "Windowed resolution with zero refresh did not round-trip");
  {
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    require(bytes.find("Windowed") != std::string::npos,
            "windowed display key was not persisted");
  }
  NativeVideoSettings borderless{};
  borderless.save(path);
  require(NativeVideoSettings::load(path) == borderless,
          "Borderless display state did not round-trip");
  {
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    require(bytes.find("Borderless") != std::string::npos,
            "legacy Borderless display key changed during persistence");
  }

  // Malformed files fall back to defaults instead of throwing.
  { std::ofstream output(path, std::ios::trunc); output << "{not json"; }
  const auto fallback = NativeVideoSettings::load(path);
  require(fallback == defaults, "malformed file did not load defaults");

  // Unknown enum strings are ignored field-by-field.
  {
    std::ofstream output(path, std::ios::trunc);
    output << R"({"display":"Nonsense","vsync":"Off","frameCap":"Fps60"})";
  }
  const auto partial = NativeVideoSettings::load(path);
  require(partial.display == VideoDisplayMode::Borderless &&
              partial.vsync == VideoVsync::Off &&
              partial.frame_cap == VideoFrameCap::Fps60,
          "partial settings did not sanitize field-by-field");
  {
    std::ofstream output(path, std::ios::trunc);
    output << R"({"display":"Exclusive","width":-1,"height":999999,"refreshHz":9999})";
  }
  const auto invalid_resolution = NativeVideoSettings::load(path);
  require(invalid_resolution.width == 0 && invalid_resolution.height == 0 &&
              invalid_resolution.refresh_hz == 0.f,
          "invalid exclusive resolution was not reset to desktop default");
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::string(64 * 1024 + 1, 'x');
  }
  require(NativeVideoSettings::load(path) == defaults,
          "oversized settings did not use the safe fallback");
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "{\"display\":\"Exclusive\"";
  }
  require(NativeVideoSettings::load(path) == defaults,
          "truncated settings did not use the safe fallback");
  bool failed_save = false;
  const auto blocked = directory / "blocked-destination";
  std::filesystem::create_directory(blocked, error);
  try { saved.save(blocked); } catch (const std::exception &) { failed_save = true; }
  require(failed_save, "save failure was silently ignored");
  saved.save(path);
  saved.frame_cap = VideoFrameCap::Fps60;
  saved.save(path);
  require(NativeVideoSettings::load(path).frame_cap == VideoFrameCap::Fps60,
          "repeated atomic settings overwrite did not retain the latest value");
  std::filesystem::remove_all(directory, error);
}

void choice_cycles() {
  constexpr int width = 1280, height = 720;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  NativeVideoSettingsView view;
  view.open(NativeVideoSettings{});
  view.set_display_choices({{1920, 1080, 60.f}, {2560, 1440, 144.f}}, "1920 x 1080 @ 60 Hz");
  require(view.visible(), "view not visible after open");

  // Display: Borderless → Exclusive → Windowed → Borderless.
  auto result = view.handle(press(InputEventType::LeftPressed,
                                  center(layout.choice_buttons[0])), width, height);
  require(result.command == VideoSettingsCommand::None && result.captured &&
              result.values.display == VideoDisplayMode::Exclusive,
          "display did not cycle to exclusive");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[0])), width, height);
  require(result.values.display == VideoDisplayMode::Windowed,
          "display did not cycle to windowed");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[0])), width, height);
  require(result.values.display == VideoDisplayMode::Borderless,
          "display did not wrap");

  // Borderless follows the desktop and cannot select an exclusive resolution.
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[1])), width, height);
  require(result.values.width == 0 && result.values.height == 0,
          "borderless mode allowed a resolution choice");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[0])), width, height);
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[1])), width, height);
  require(result.values.width == 1920 && result.values.height == 1080 &&
              result.values.refresh_hz == 60.f,
          "exclusive mode did not select a detected resolution");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[1])), width, height);
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[1])), width, height);
  require(result.values.width == 0 && result.values.height == 0 &&
              result.values.refresh_hz == 0.f,
          "resolution cycle did not include the desktop default");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_previous[1])), width, height);
  require(result.values.width == 2560 && result.values.height == 1440,
          "resolution previous control did not wrap from desktop to the last mode");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_previous[1])), width, height);
  require(result.values.width == 1920 && result.values.height == 1080,
          "resolution previous control did not step backward through detected modes");

  // V-Sync: On → Adaptive → Off → On.
  for (const VideoVsync expected : {VideoVsync::Adaptive, VideoVsync::Off,
                                    VideoVsync::On}) {
    result = view.handle(press(InputEventType::LeftPressed,
                               center(layout.choice_buttons[2])), width, height);
    require(result.values.vsync == expected, "vsync did not cycle");
  }
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_previous[2])), width, height);
  require(result.values.vsync == VideoVsync::Off,
          "V-Sync previous control did not wrap backward");

  // Frame cap: Automatic → 60 → 120 → 144 → Unlimited → Automatic.
  for (const VideoFrameCap expected :
       {VideoFrameCap::Fps60, VideoFrameCap::Fps120, VideoFrameCap::Fps144,
        VideoFrameCap::Unlimited, VideoFrameCap::Automatic}) {
    result = view.handle(press(InputEventType::LeftPressed,
                               center(layout.choice_buttons[3])), width, height);
    require(result.values.frame_cap == expected, "frame cap did not cycle");
  }
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_previous[3])), width, height);
  require(result.values.frame_cap == VideoFrameCap::Unlimited,
          "frame-cap previous control did not wrap backward");
}

void same_resolution_refresh_choices_and_windowed_sizes() {
  constexpr int width = 1280, height = 720;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  NativeVideoSettingsView view;
  view.open(NativeVideoSettings{});
  view.set_display_choices({{1280, 720, 60.f}, {1280, 720, 144.f},
                            {1920, 1080, 60.f}},
                           "test display");
  // A window-size list is intentionally independent from exclusive refresh
  // tuples and may be deduped to one choice for each logical size.
  view.set_windowed_choices({{1280, 720, 0.f}, {1280, 720, 0.f},
                             {1920, 1080, 0.f}});
  auto result = view.handle(press(InputEventType::LeftPressed,
                                  center(layout.choice_next[0])), width, height);
  require(result.values.display == VideoDisplayMode::Exclusive,
          "display did not enter Exclusive mode");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[1])), width, height);
  require(result.values.width == 1280 && result.values.height == 720 &&
              result.values.refresh_hz == 60.f,
          "exclusive choices did not begin at the first refresh tuple");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[1])), width, height);
  require(result.values.width == 1280 && result.values.height == 720 &&
              result.values.refresh_hz == 144.f,
          "same-size exclusive refresh choice was lost or stuck");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[1])), width, height);
  require(result.values.width == 1920 && result.values.height == 1080 &&
              result.values.refresh_hz == 60.f,
          "exclusive size did not advance after same-size refresh choices");

  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[0])), width, height);
  require(result.values.display == VideoDisplayMode::Windowed &&
              result.values.width == 1920 && result.values.height == 1080 &&
              result.values.refresh_hz == 0.f,
          "Windowed mode did not retain its size and clear refresh rate");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[1])), width, height);
  require(result.values.width == 0 && result.values.height == 0 &&
              result.values.refresh_hz == 0.f,
          "Windowed size cycle did not expose its default-size choice");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[1])), width, height);
  require(result.values.width == 1280 && result.values.height == 720 &&
              result.values.refresh_hz == 0.f,
          "Windowed size choices did not advance independently of exclusive modes");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[1])), width, height);
  require(result.values.width == 1920 && result.values.height == 1080 &&
              result.values.refresh_hz == 0.f,
          "Windowed choices did not retain both deduped logical sizes");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_next[0])), width, height);
  require(result.values.display == VideoDisplayMode::Borderless &&
              result.values.width == 0 && result.values.height == 0 &&
              result.values.refresh_hz == 0.f,
          "legacy Borderless display state changed after Windowed cycling");
}

void apply_confirm_revert_flow() {
  constexpr int width = 1280, height = 720;
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  NativeVideoSettingsView view;
  view.open(NativeVideoSettings{});

  // Apply emits the draft payload.
  auto result = view.handle(
      press(InputEventType::LeftPressed, center(layout.apply)), width, height);
  require(result.command == VideoSettingsCommand::Apply && result.captured,
          "Apply did not emit its command");

  // The host raises the confirm overlay; while it is up only Keep/Revert
  // answer, and panel clicks are swallowed.
  view.set_confirming(true);
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.choice_buttons[1])), width, height);
  require(result.command == VideoSettingsCommand::None && result.captured,
          "confirm overlay leaked input to the panel");
  result = view.handle(press(InputEventType::EscapePressed, {}), width, height);
  require(result.command == VideoSettingsCommand::Revert,
          "Escape did not revert a pending display preview");
  view.set_confirming(false);
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.apply)), width, height);
  view.set_confirming(true);
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.revert)), width, height);
  require(result.command == VideoSettingsCommand::Revert,
          "Revert did not emit its command");
  view.set_confirming(false);

  // Apply again, then Keep.
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.apply)), width, height);
  view.set_confirming(true);
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.keep)), width, height);
  require(result.command == VideoSettingsCommand::Keep,
          "Keep did not emit its command");
  view.set_confirming(false);

  // Cancel and Escape both emit Cancel.
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.cancel)), width, height);
  require(result.command == VideoSettingsCommand::Cancel,
          "Cancel did not emit its command");
  result = view.handle(press(InputEventType::EscapePressed, {}), width, height);
  require(result.command == VideoSettingsCommand::Cancel,
          "Escape did not emit Cancel");
  view.close();
  require(!view.visible(), "view stayed visible after close");
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.apply)), width, height);
  require(result.command == VideoSettingsCommand::None && !result.captured,
          "closed view captured input");
}

} // namespace

int main() {
  try {
    responsive_layout();
    persistence_round_trip();
    choice_cycles();
    same_resolution_refresh_choices_and_windowed_sizes();
    apply_confirm_revert_flow();
  } catch (const std::exception &error) {
    std::cerr << "native video settings tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native video settings tests passed\n";
  return 0;
}
