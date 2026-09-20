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
    require(layout.choice_labels.size() == 8 && layout.choice_buttons.size() == 8,
            "choice rows incomplete");
    for (int index = 0; index < 4; ++index) {
      require(inside(layout.choice_buttons[index]), "choice escaped");
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
  saved.starfield_quality = 3;
  saved.starfield_density = 0;
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

void startup_preserves_desktop() {
  NativeVideoSettings saved{.display=VideoDisplayMode::Exclusive,.width=2560,.height=1440,.refresh_hz=144.f,.vsync=VideoVsync::Adaptive,.frame_cap=VideoFrameCap::Fps144,.scene_resolution_percent=75,.scene_samples=4,.starfield_quality=3,.starfield_density=2};
  auto expected=saved;expected.display=VideoDisplayMode::Borderless;
  require(saved.for_startup()==expected&&saved.display==VideoDisplayMode::Exclusive,"Startup lost display preferences or retained exclusive mode");
  saved.display=VideoDisplayMode::Windowed;saved.refresh_hz=0;
  require(saved.for_startup()==saved,"Startup did not preserve Windowed mode");
}

void dropdown_choices() {
  constexpr int width=1280,height=720;
  const auto layout=VideoSettingsLayout::for_viewport(width,height);
  NativeVideoSettingsView view;view.open({});
  view.set_display_choices({{1280,720,60.f},{1280,720,144.f},{1920,1080,60.f}},"test display");
  view.set_windowed_choices({{1280,720,0.f},{1280,720,0.f},{1920,1080,0.f}});
  auto select=[&](int row,int count,int option){
    const auto before=view.values();
    (void)view.handle(press(InputEventType::LeftPressed,center(layout.choice_buttons[row])),width,height);
    require(view.values()==before,"Opening dropdown changed the value");
    stellar::native_ui::Dropdown popup;popup.open(row,std::vector<std::string>(count,"item"),0);
    const auto choices=popup.layout(layout.choice_buttons[row],width,height);
    return view.handle(press(InputEventType::LeftPressed,center(choices.rows[option])),width,height);
  };
  select(0,3,1);require(view.values().display==VideoDisplayMode::Exclusive,"Exclusive selection failed");
  select(1,4,2);require(view.values().width==1280&&view.values().refresh_hz==144.f,"Same-resolution refresh tuples were not distinct");
  select(1,4,3);require(view.values().width==1920,"Resolution direct selection failed");
  select(0,3,2);require(view.values().display==VideoDisplayMode::Windowed&&view.values().refresh_hz==0,"Windowed selection retained refresh");
  select(1,3,1);require(view.values().width==1280,"Windowed resolution was not deduplicated");
  select(1,3,0);require(view.values().width==0,"Default window size missing");
  select(2,3,0);select(3,5,4);select(4,3,2);select(5,3,0);select(6,4,3);select(7,3,0);
  require(view.values().starfield_quality==3&&view.values().starfield_density==0,"Starfield dropdown values failed");
  require(view.values().vsync==VideoVsync::Off&&view.values().frame_cap==VideoFrameCap::Unlimited&&view.values().scene_samples==4&&view.values().scene_resolution_percent==50,"Quality dropdown values failed");
  (void)view.handle(press(InputEventType::LeftPressed,center(layout.choice_buttons[0])),width,height);
  require(view.handle(press(InputEventType::EscapePressed,{}),width,height).command==VideoSettingsCommand::None&&view.visible(),"Escape closed settings instead of just the dropdown");
  (void)view.handle(press(InputEventType::LeftPressed,center(layout.choice_buttons[0])),width,height);
  require(view.handle(press(InputEventType::LeftPressed,center(layout.apply)),width,height).command==VideoSettingsCommand::None,"Outside click fell through dropdown to Apply");
  select(0,3,0);const auto before=view.values();
  (void)view.handle(press(InputEventType::LeftPressed,center(layout.choice_buttons[1])),width,height);
  require(view.values()==before,"Borderless resolution changed");
}

void long_dropdown_navigation() {
  using stellar::native_ui::Dropdown;
  for(const auto [width,height]:{std::pair{1280,720},{1920,1080},{2560,1440},{3440,1440},{3840,2160}}){
    Dropdown menu;std::vector<std::string> options(100,"resolution");menu.open(1,options,0);
    const UiRect anchor{static_cast<float>(width-400),static_cast<float>(height-48),360,36};
    auto l=menu.layout(anchor,width,height);
    require(l.panel.y>=0&&l.panel.y+l.panel.height<=height&&l.rows.size()==8,"Dropdown list overflowed viewport");
    InputEvent wheel{InputEventType::Wheel,center(l.panel)};wheel.wheel_y=-1000;menu.handle(wheel,anchor,width,height);
    require(menu.handle(press(InputEventType::LeftPressed,center(l.rows.back())),anchor,width,height)==99,"Scrolling did not expose last detected resolution");
    menu.open(1,options,0);InputEvent key{InputEventType::KeyPressed};key.key=0x4000004du;menu.handle(key,anchor,width,height);key.key=13;
    require(menu.handle(key,anchor,width,height)==99,"Keyboard selection did not reach last entry");
  }
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
    startup_preserves_desktop();
    dropdown_choices();
    long_dropdown_navigation();
    apply_confirm_revert_flow();
  } catch (const std::exception &error) {
    std::cerr << "native video settings tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native video settings tests passed\n";
  return 0;
}
