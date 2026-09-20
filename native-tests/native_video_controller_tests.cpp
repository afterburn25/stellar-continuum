#include "native_video_controller.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace stellar::native_video_settings;
using namespace stellar::native_map;

constexpr int width = 1280;
constexpr int height = 720;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
Point center(UiRect rect) {
  return {rect.x + rect.width * .5f, rect.y + rect.height * .5f};
}
InputEvent click(Point point) {
  InputEvent event{};
  event.type = InputEventType::LeftPressed;
  event.position = point;
  return event;
}
InputEvent event(InputEventType type) {
  InputEvent value{};
  value.type = type;
  return value;
}

struct Backend {
  std::vector<NativeVideoSettings> calls;
  std::vector<int> throwing_calls;
  void apply(const NativeVideoSettings &values) {
    calls.push_back(values);
    if (std::ranges::find(throwing_calls, static_cast<int>(calls.size())) !=
        throwing_calls.end())
      throw std::runtime_error("backend rejected mode");
  }
};

struct Fixture {
  NativeVideoController::Clock::time_point now{};
  Backend backend;
  std::vector<NativeVideoSettings> persisted;
  bool persist_throws{};
  std::filesystem::path path{std::filesystem::temp_directory_path() /
                             "stellar-video-controller-headless.json"};

  NativeVideoController make() {
    return NativeVideoController(
        path, [this](const auto &value) { backend.apply(value); },
        [this] { return now; }, [this](const auto &value) {
          if (persist_throws) throw std::runtime_error("settings storage unavailable");
          persisted.push_back(value);
        });
  }
};

void select_display(NativeVideoController& controller,int index) {
  const auto layout=VideoSettingsLayout::for_viewport(width,height);
  controller.handle(click(center(layout.choice_buttons[0])),width,height);
  stellar::native_ui::Dropdown menu;menu.open(0,{"Borderless","Exclusive","Windowed"},0);
  controller.handle(click(center(menu.layout(layout.choice_buttons[0],width,height).rows[index])),width,height);
}
void choose_exclusive_and_apply(NativeVideoController& controller) {
  select_display(controller,1);
  controller.handle(click(center(VideoSettingsLayout::for_viewport(width,height).apply)),width,height);
}
void choose_windowed_and_apply(NativeVideoController& controller) {
  controller.set_display_choices({{1280,720,60.f},{1920,1080,60.f}},"test display");
  controller.set_windowed_display_choices({{1280,720,0.f},{1920,1080,0.f}});
  select_display(controller,2);
  controller.handle(click(center(VideoSettingsLayout::for_viewport(width,height).apply)),width,height);
}

void partial_apply_is_restored() {
  Fixture fixture;
  auto controller = fixture.make();
  controller.open();
  fixture.backend.throwing_calls = {2}; // Preview changes state before rejecting it.
  choose_exclusive_and_apply(controller);
  require(!controller.previewing() && controller.active() == NativeVideoSettings{},
          "rejected partial preview did not restore the previous active settings");
  require(fixture.backend.calls.size() == 3 &&
              fixture.backend.calls[1].display == VideoDisplayMode::Exclusive &&
              fixture.backend.calls[2] == NativeVideoSettings{},
          "partial backend apply was not followed by an explicit restoration");
  require(controller.visible() && controller.notice().find("rejected") != std::string::npos,
          "rejected preview did not reopen an actionable error view");
}

void keep_persists_or_rolls_back() {
  Fixture fixture;
  auto controller = fixture.make();
  controller.open();
  choose_exclusive_and_apply(controller);
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  controller.handle(click(center(layout.keep)), width, height);
  require(!controller.previewing() && !controller.visible() &&
              controller.active().display == VideoDisplayMode::Exclusive &&
              fixture.persisted.size() == 1,
          "Keep did not persist and close a successful preview");

  Fixture failed;
  failed.persist_throws = true;
  auto rejected_keep = failed.make();
  rejected_keep.open();
  choose_exclusive_and_apply(rejected_keep);
  rejected_keep.handle(click(center(layout.keep)), width, height);
  require(!rejected_keep.previewing() &&
              rejected_keep.active() == NativeVideoSettings{} &&
              failed.backend.calls.size() == 3 &&
              failed.backend.calls.back() == NativeVideoSettings{},
          "failed Keep did not roll the display back before reporting failure");
}

void windowed_keep_and_revert_preserve_saved_preferences() {
  Fixture kept;
  auto controller = kept.make();
  controller.open();
  choose_windowed_and_apply(controller);
  require(controller.previewing() && controller.active().display == VideoDisplayMode::Windowed &&
              controller.active().width == 1280 && controller.active().height == 720 &&
              controller.active().refresh_hz == 0.f,
          "Windowed preview did not retain its logical client resolution");
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  controller.handle(click(center(layout.keep)), width, height);
  require(kept.persisted.size() == 1 &&
              kept.persisted.front().display == VideoDisplayMode::Windowed &&
              kept.persisted.front().width == 1280 && kept.persisted.front().height == 720,
          "Keep did not persist the selected Windowed settings");

  Fixture reverted;
  auto rollback = reverted.make();
  rollback.open();
  choose_windowed_and_apply(rollback);
  rollback.handle(click(center(layout.revert)), width, height);
  require(!rollback.previewing() && rollback.active() == NativeVideoSettings{} &&
              reverted.persisted.empty() && reverted.backend.calls.size() == 3 &&
              reverted.backend.calls.back() == NativeVideoSettings{},
          "Revert changed saved preferences or failed to restore the prior mode");
}

void timeout_and_late_keep_are_safe() {
  Fixture fixture;
  auto controller = fixture.make();
  controller.open();
  choose_exclusive_and_apply(controller);
  fixture.now += std::chrono::seconds(15);
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  controller.handle(click(center(layout.keep)), width, height);
  require(!controller.previewing() && controller.active() == NativeVideoSettings{} &&
              fixture.persisted.empty() && fixture.backend.calls.size() == 3,
          "late Keep persisted an expired display preview");
}

void inactive_window_reverts() {
  Fixture fixture;
  auto controller = fixture.make();
  controller.open();
  choose_exclusive_and_apply(controller);
  controller.service(false, true);
  require(controller.previewing(),"Transient display-switch focus loss reverted the preview");
  fixture.now += std::chrono::seconds(2);
  controller.service(false, true);
  require(!controller.previewing() && controller.active() == NativeVideoSettings{},
          "focus loss did not revert the preview");
  controller.open();
  choose_exclusive_and_apply(controller);
  fixture.now += std::chrono::seconds(2);
  controller.service(true, false);
  require(!controller.previewing() && controller.active() == NativeVideoSettings{},
          "non-renderable window did not revert the preview");
}

void confirmation_escape_cancel_and_close_restore() {
  Fixture fixture;
  auto controller = fixture.make();
  controller.open();
  choose_exclusive_and_apply(controller);
  controller.handle(event(InputEventType::EscapePressed), width, height);
  require(!controller.previewing() && controller.active() == NativeVideoSettings{},
          "Escape did not revert the confirmation preview");
  controller.open();
  choose_exclusive_and_apply(controller);
  controller.handle(event(InputEventType::PointerCancelled), width, height);
  require(!controller.previewing() && controller.active() == NativeVideoSettings{},
          "pointer cancellation did not revert the confirmation preview");
  controller.open();
  choose_exclusive_and_apply(controller);
  controller.close();
  require(!controller.previewing() && controller.active() == NativeVideoSettings{},
          "closing the panel left a preview active");

  Fixture destruction;
  {
    auto preview = destruction.make();
    preview.open();
    choose_exclusive_and_apply(preview);
  }
  require(destruction.backend.calls.size() == 3 &&
              destruction.backend.calls.back() == NativeVideoSettings{},
          "controller destruction left a preview active");
}

void recovery_and_latched_failure() {
  Fixture recovered;
  auto controller = recovered.make();
  controller.open();
  choose_exclusive_and_apply(controller);
  recovered.backend.throwing_calls = {3}; // Rollback fails, safe recovery succeeds.
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  controller.handle(click(center(layout.revert)), width, height);
  NativeVideoSettings safe{};
  safe.vsync = VideoVsync::Off;
  require(!controller.previewing() && controller.active() == safe &&
              recovered.backend.calls.size() == 4 &&
              recovered.backend.calls.back() == safe,
          "rollback failure did not apply safe recovery settings");

  Fixture latched;
  auto controller_latched = latched.make();
  controller_latched.open();
  choose_exclusive_and_apply(controller_latched);
  // Revert calls restore (3), then one safe recovery attempt (4); both fail.
  latched.backend.throwing_calls = {3, 4};
  controller_latched.handle(click(center(layout.revert)), width, height);
  require(!controller_latched.backend_state_known(), "failed recovery claimed a known display state");
  const auto attempts = latched.backend.calls.size();
  controller_latched.open();
  controller_latched.handle(click(center(layout.apply)), width, height);
  require(latched.backend.calls.size() == attempts,
          "latched recovery failure retried the backend after faulting");
}
void launch_override_is_editable_and_not_persisted(){
  Fixture f;NativeVideoSettings launch;launch.display=VideoDisplayMode::Windowed;launch.width=1280;launch.height=720;
  NativeVideoController controller(f.path,[&](const auto& value){f.backend.apply(value);},[&]{return f.now;},[&](const auto& value){f.persisted.push_back(value);},launch);
  require(controller.active()==launch&&f.backend.calls.back()==launch&&f.persisted.empty(),"Launch override was not applied independently of saved preferences");
  controller.set_windowed_display_choices({{1280,720,0}});controller.open();
  const auto layout=VideoSettingsLayout::for_viewport(width,height);
  select_display(controller,1);controller.handle(click(center(layout.apply)),width,height);
  require(controller.previewing()&&controller.active().display!=VideoDisplayMode::Windowed,"Windowed launch locked display controls");
  controller.close();require(controller.active()==launch&&f.backend.calls.back()==launch&&f.persisted.empty(),"Cancelling launch-mode preview did not restore without saving");
}
} // namespace

int main() try {
  partial_apply_is_restored();
  keep_persists_or_rolls_back();
  windowed_keep_and_revert_preserve_saved_preferences();
  timeout_and_late_keep_are_safe();
  inactive_window_reverts();
  confirmation_escape_cancel_and_close_restore();
  recovery_and_latched_failure();
  launch_override_is_editable_and_not_persisted();
  std::cout << "native video controller tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native video controller tests failed: " << error.what() << '\n';
  return 1;
}
