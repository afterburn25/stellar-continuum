#include "native_startup_entry.hpp"

#include <chrono>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace stellar::native_startup_ui {
using namespace stellar::native_map;
using namespace stellar::native_startup;
using namespace stellar::native_setup;

namespace {
Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}
} // namespace

StartupEntryResult run_native_startup_entry(Window &window,
                                             StartupEntryConfig config,
                                             const StartupEntryAutomation *automation) {
  if (!config.utc_timestamp)
    throw std::invalid_argument("Startup requires a UTC timestamp provider.");
  NativeStartupHost host(config.host);
  NativeStartupWorkspace workspace;
  const auto setup_view = host.setup();
  workspace.set_setup(setup_view);
  std::unordered_map<std::string, std::shared_ptr<const RgbaImage>> portraits;
  NativeStartupWorkspace::PortraitProvider portrait_provider =
      [&](std::string_view relative) {
        auto [entry, inserted] = portraits.try_emplace(std::string(relative));
        if (inserted)
          entry->second = decode_rgba_image(config.asset_root / entry->first);
        return entry->second;
      };
  auto measure = [&](const Text &text) { return window.measure_text(text); };
  StartupEntryEvidence evidence;
  const auto dispatch = [&](const StartupIntent &intent) {
    switch (intent.kind) {
    case StartupIntentKind::OpenLoad:
      workspace.set_slots(host.slots());
      break;
    case StartupIntentKind::Create: {
      const auto started = host.start_new(
          {intent.seed_text, intent.system_count, intent.species_id,
           config.utc_timestamp()});
      if (started.accepted)
        workspace.set_operation(host.poll());
      else
        workspace.set_setup_message(started.message, false);
      break;
    }
    case StartupIntentKind::LoadSelected: {
      const auto started = host.start_load(intent.save_path);
      if (started.accepted)
        workspace.set_operation(host.poll());
      else {
        std::cerr << started.message << '\n';
        workspace.show_failure(started.message);
      }
      break;
    }
    case StartupIntentKind::CancelOperation:
      (void)host.cancel();
      workspace.set_operation(host.poll());
      break;
    default:
      break;
    }
  };
  if (automation) {
    const int width = window.drawable_width(), height = window.drawable_height();
    evidence.entry_opened = workspace.screen() == StartupScreen::Entry;
    const auto entry_layout = StartupLayout::for_viewport(width, height);
    auto intent = workspace.handle(
        {InputEventType::LeftPressed, center(entry_layout.new_campaign)}, width,
        height, measure);
    evidence.setup_opened = intent.kind == StartupIntentKind::OpenSetup &&
                            workspace.screen() == StartupScreen::Setup;
    const auto option = std::ranges::find(setup_view.species,
                                          automation->species_id,
                                          &NativeSpeciesSetupOption::id);
    const auto size = std::ranges::find(setup_view.size_presets,
                                        automation->system_count,
                                        &NativeGalaxySizeOption::system_count);
    if (option == setup_view.species.end() || size == setup_view.size_presets.end())
      throw std::runtime_error("Startup automation requested an unavailable setup choice.");
    stellar::native_setup_ui::NativeNewGameWorkspace layout_source;
    layout_source.set_view(setup_view);
    const auto measured = layout_source.measure_layout(width, height, measure);
    const auto species_index = static_cast<std::size_t>(option - setup_view.species.begin());
    intent = workspace.handle(
        {InputEventType::LeftPressed, center(measured.species_rows[species_index])},
        width, height, measure);
    evidence.species_selected = intent.captured;
    const auto size_index = static_cast<std::size_t>(size - setup_view.size_presets.begin());
    if (size_index >= measured.base.size_buttons.size())
      throw std::runtime_error("Startup automation size has no visible selector.");
    (void)workspace.handle(
        {InputEventType::LeftPressed, center(measured.base.size_buttons[size_index])},
        width, height, measure);
    evidence.size_selected = true;
    (void)workspace.handle(
        {InputEventType::LeftPressed, center(measured.base.seed_input)}, width,
        height, measure);
    (void)workspace.handle(
        {InputEventType::TextEntered, {}, {}, 0, automation->seed_text}, width,
        height, measure);
    evidence.seed_entered = true;
    DrawList setup_draw;
    workspace.render(setup_draw, width, height, measure, &portrait_provider);
    window.draw(setup_draw, automation->setup_screenshot);
    intent = workspace.handle(
        {InputEventType::LeftPressed, center(measured.base.create)}, width,
        height, measure);
    evidence.create_requested = intent.kind == StartupIntentKind::Create;
    evidence.species_selected = evidence.species_selected &&
                                intent.species_id == automation->species_id;
    evidence.size_selected = evidence.size_selected &&
                             intent.system_count == automation->system_count;
    evidence.seed_entered = evidence.seed_entered &&
                            intent.seed_text == automation->seed_text;
    dispatch(intent);
    if (workspace.screen() != StartupScreen::Busy)
      throw std::runtime_error("Automated startup did not begin generation.");
    const auto displayed = host.poll();
    evidence.indeterminate_observed = !displayed.determinate_progress.has_value();
    evidence.displayed_statuses.push_back(displayed.status);
    DrawList loading_draw;
    workspace.render(loading_draw, width, height, measure, &portrait_provider);
    window.draw(loading_draw, automation->loading_screenshot);
  }
  for (;;) {
    const auto input = window.poll();
    if (input.quit_requested) return {{}, true};
    if (!input.renderable()) {
      for (const auto &event : input.events)
        (void)workspace.handle(event, input.drawable_width,
                               input.drawable_height, measure);
      window.set_text_input(workspace.wants_text_input());
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      continue;
    }
    bool exit{};
    for (const auto &event : input.events) {
      const auto intent = workspace.handle(event, input.drawable_width,
                                           input.drawable_height, measure);
      switch (intent.kind) {
      case StartupIntentKind::Exit:
        exit = true;
        break;
      default:
        dispatch(intent);
        break;
      }
    }
    if (exit) return {{}, true};
    if (workspace.screen() == StartupScreen::Busy) {
      host.service();
      const auto state = host.poll();
      if (evidence.displayed_statuses.empty() ||
          evidence.displayed_statuses.back() != state.status)
        evidence.displayed_statuses.push_back(state.status);
      if (state.phase == NativeStartupPhase::Ready) {
        auto session = host.take_ready();
        if (!session)
          throw std::runtime_error("Ready startup session was unavailable.");
        window.set_text_input(false);
        return {std::move(session), false, std::move(evidence)};
      }
      if (state.phase == NativeStartupPhase::Failed) {
        std::cerr << state.status << '\n';
        workspace.show_failure(state.status);
      } else if (state.phase == NativeStartupPhase::Cancelled)
        workspace.show_entry();
      else
        workspace.set_operation(state);
    }
    window.set_text_input(workspace.wants_text_input());
    DrawList draw;
    workspace.render(draw, input.drawable_width, input.drawable_height, measure,
                     &portrait_provider);
    window.draw(draw);
  }
}
} // namespace stellar::native_startup_ui
