#include "native_startup_entry.hpp"
#include "native_audio_settings.hpp"
#include "native_video_controller.hpp"
#include "native_video_settings_smoke.hpp"
#include "native_audio_settings_smoke.hpp"

#include <algorithm>
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
  if (config.minimum_boot_artwork < std::chrono::milliseconds::zero())
    throw std::invalid_argument("Startup artwork minimum duration cannot be negative.");
  NativeStartupHost host(config.host);
  NativeStartupWorkspace workspace;
  const auto setup_view = host.setup();
  workspace.set_setup(setup_view);
  workspace.set_return_to_campaign_available(
      config.return_to_campaign_available);
  if (config.return_to_campaign_available)
    workspace.show_setup();
  std::unordered_map<std::string, std::shared_ptr<const RgbaImage>> portraits;
  NativeStartupWorkspace::PortraitProvider portrait_provider =
      [&](std::string_view relative) {
        auto [entry, inserted] = portraits.try_emplace(std::string(relative));
        if (inserted)
          entry->second = decode_rgba_image(config.asset_root / entry->first);
        return entry->second;
      };
  NativeStartupArtworkAssets artwork_assets(config.asset_root);
  StartupArtworkProvider artwork_provider = [&](StartupArtworkKind kind) {
    return artwork_assets.image(kind);
  };
  auto measure = [&](const Text &text) { return window.measure_text(text); };
  StartupEntryEvidence evidence;

  // The normal application shows the approved startup artwork for at least
  // seven seconds while the four immutable startup images are staged. Smoke
  // automation disables only the minimum dwell; it still draws the boot frame
  // and loads the same resources before interacting with the menu.
  if (!config.return_to_campaign_available) {
    const auto minimum_boot = automation ? std::chrono::milliseconds::zero()
                                         : config.minimum_boot_artwork;
    const auto boot_started = std::chrono::steady_clock::now();
    auto boot = artwork_assets.image(StartupArtworkKind::ApplicationStartup);
    std::size_t staged = 1;
    for (;;) {
    if (config.audio.service) config.audio.service();
    const bool audio_ready = !config.audio.assets_ready || config.audio.assets_ready();
    const auto input = window.poll();
    if (config.video_settings) config.video_settings->service(input.focused, input.renderable());
    if (input.quit_requested) {
      evidence.exit_requested = true;
      return {{}, true, std::move(evidence)};
    }
    if (input.renderable()) {
      evidence.boot_presented = true;
      DrawList draw;
      const auto destination = startup_artwork_destination(
          boot->width(), boot->height(), input.drawable_width,
          input.drawable_height);
      const UiRect viewport{0, 0, static_cast<float>(input.drawable_width),
                            static_cast<float>(input.drawable_height)};
      draw.overlay.emplace_back(
          Image{boot, destination, std::nullopt, {255, 255, 255, 255}, viewport});
      const float scale = std::clamp(input.drawable_height / 900.f, 1.f, 2.4f);
      const UiRect veil{input.drawable_width * .18f,
                        input.drawable_height - 116.f * scale,
                        input.drawable_width * .64f, 76.f * scale};
      draw.overlay.emplace_back(FilledRectangle{veil, {5, 14, 27, 210}});
      draw.overlay.emplace_back(Text{{veil.x + veil.width * .5f,
                                      veil.y + 12.f * scale},
                                     "LOADING GAME ASSETS", {235,244,255,255},
                                     static_cast<int>(18 * scale), veil.width,
                                     veil, TextAlign::Center, FontFace::Heading});
      const UiRect track{veil.x + 26.f * scale, veil.y + 48.f * scale,
                         veil.width - 52.f * scale, 8.f * scale};
      draw.overlay.emplace_back(FilledRectangle{track, {12,31,54,245}});
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - boot_started);
      const float progress = static_cast<float>(
          startup_boot_progress(staged + (config.audio.assets_ready && audio_ready ? 1u : 0u),
                                config.audio.assets_ready ? 5u : 4u, elapsed, minimum_boot));
      draw.overlay.emplace_back(FilledRectangle{
          {track.x, track.y, track.width * progress, track.height},
          {122,230,190,255}});
      window.draw(draw);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - boot_started);
    if (staged == 4 && audio_ready && elapsed >= minimum_boot)
      break;
    if (staged < 4) {
      (void)artwork_assets.image(static_cast<StartupArtworkKind>(staged));
      ++staged;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (config.audio.menu_ready) {
      config.audio.menu_ready();
      evidence.menu_ready_called = true;
    }
  }
  const auto dispatch = [&](const StartupIntent &intent) {
    if (intent.kind != StartupIntentKind::None && config.audio.confirm)
      config.audio.confirm();
    switch (intent.kind) {
    case StartupIntentKind::OpenSettings:
      if (config.audio_settings) config.audio_settings->open();
      break;
    case StartupIntentKind::OpenLoad:
      workspace.set_slots(host.slots());
      break;
    case StartupIntentKind::Create: {
      const auto started = host.start_new(
          {intent.seed_text, intent.system_count, intent.species_id,
           config.utc_timestamp()});
      if (started.accepted)
        workspace.begin_operation(host.poll(),
                                  StartupOperationOrigin::NewCampaign);
      else
        workspace.set_setup_message(started.message, false);
      break;
    }
    case StartupIntentKind::LoadSelected: {
      const auto started = host.start_load(intent.save_path);
      if (started.accepted)
        workspace.begin_operation(host.poll(),
                                  StartupOperationOrigin::SavedCampaign);
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
    if (!automation->audio_settings_screenshot.empty()) {
      if (!config.audio_settings) throw std::runtime_error("Audio settings validation requires the real overlay.");
      stellar::native_audio::check_audio_settings(*config.audio_settings,
          automation->audio_settings_path, width, height, "startup",
          [&] { dispatch(workspace.handle({InputEventType::LeftPressed, center(entry_layout.settings)}, width, height, measure)); },
          [&](const InputEvent& event) { (void)config.audio_settings->handle(event, width, height); },
          [&] {
            DrawList draw;
            workspace.render(draw, width, height, measure, &portrait_provider, &artwork_provider);
            config.audio_settings->render(draw, width, height);
            window.draw(draw, automation->audio_settings_screenshot);
          });
    }
    if (!automation->video_settings_screenshot.empty()) {
      if (!config.audio_settings || !config.video_settings) throw std::runtime_error("Video settings validation requires the menu overlays.");
      const auto route=[&](const InputEvent& event){
        if(config.video_settings->visible())(void)config.video_settings->handle(event,width,height);
        else if(config.audio_settings->visible())(void)config.audio_settings->handle(event,width,height);
        else dispatch(workspace.handle(event,width,height,measure));
      };
      stellar::native_video_settings::check_video_settings(*config.video_settings,
          automation->video_settings_path,width,height,"startup",
          [&]{route({InputEventType::LeftPressed,center(entry_layout.settings)});
              route({InputEventType::LeftPressed,center(stellar::native_audio::AudioSettingsLayout::for_viewport(width,height).video)});},route,
          [&](bool confirming){DrawList draw;
            workspace.render(draw,width,height,measure,&portrait_provider,&artwork_provider);
            config.video_settings->render(draw,width,height);
            window.draw(draw,confirming?automation->video_confirm_screenshot:automation->video_settings_screenshot);
          });
    }
    if (automation->action != StartupEntryAutomationAction::Create) {
      evidence.setup_opened = workspace.screen() == StartupScreen::Setup;
      if (workspace.screen() == StartupScreen::Setup) {
        const auto back = workspace.handle({InputEventType::EscapePressed},
                                           width, height, measure);
        if (back.kind != StartupIntentKind::Back ||
            workspace.screen() != StartupScreen::Entry)
          throw std::runtime_error(
              "Startup lifecycle automation could not return to Entry.");
      }
      if (workspace.screen() != StartupScreen::Entry)
        throw std::runtime_error(
            "Startup lifecycle automation requires the Entry screen.");
      DrawList entry_draw;
      workspace.render(entry_draw, width, height, measure, &portrait_provider,
                       &artwork_provider);
      window.draw(entry_draw, automation->setup_screenshot);
      const auto target = automation->action ==
                                  StartupEntryAutomationAction::ReturnToCampaign
                              ? entry_layout.return_to_campaign
                              : entry_layout.exit;
      const auto intent = workspace.handle(
          {InputEventType::LeftPressed, center(target)}, width, height, measure);
      if (automation->action == StartupEntryAutomationAction::ReturnToCampaign) {
        if (intent.kind != StartupIntentKind::ReturnToCampaign)
          throw std::runtime_error(
              "Startup lifecycle automation did not route Return to Campaign.");
        if (config.audio.confirm) config.audio.confirm();
        evidence.returned_to_campaign = true;
        window.set_text_input(false);
        return {{}, false, std::move(evidence), true};
      }
      if (intent.kind != StartupIntentKind::Exit)
        throw std::runtime_error(
            "Startup lifecycle automation did not route Exit to Windows.");
      evidence.exit_requested = true;
      window.set_text_input(false);
      return {{}, true, std::move(evidence)};
    }

    StartupIntent intent;
    if (workspace.screen() == StartupScreen::Entry) {
      intent = workspace.handle(
          {InputEventType::LeftPressed, center(entry_layout.new_campaign)},
          width, height, measure);
      evidence.setup_opened = intent.kind == StartupIntentKind::OpenSetup &&
                              workspace.screen() == StartupScreen::Setup;
    } else {
      evidence.setup_opened = workspace.screen() == StartupScreen::Setup;
      intent = {StartupIntentKind::OpenSetup, evidence.setup_opened};
    }
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
    workspace.render(setup_draw, width, height, measure, &portrait_provider,
                     &artwork_provider);
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
    workspace.render(loading_draw, width, height, measure, &portrait_provider,
                     &artwork_provider);
    window.draw(loading_draw, automation->loading_screenshot);
  }
  for (;;) {
    if (config.audio.service) config.audio.service();
    const auto input = window.poll();
    if (config.video_settings) config.video_settings->service(input.focused, input.renderable());
    if (input.quit_requested) {
      evidence.exit_requested = true;
      return {{}, true, std::move(evidence)};
    }
    if (!input.renderable()) {
      for (const auto &event : input.events)
        if (config.video_settings && config.video_settings->visible())
          (void)config.video_settings->handle(event, input.drawable_width, input.drawable_height);
        else if (config.audio_settings && config.audio_settings->visible())
          (void)config.audio_settings->handle(event, input.drawable_width, input.drawable_height);
        else (void)workspace.handle(event, input.drawable_width,
                                    input.drawable_height, measure);
      window.set_text_input(workspace.wants_text_input());
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      continue;
    }
    bool exit{};
    for (const auto &event : input.events) {
      if (config.video_settings && config.video_settings->visible()) {
        (void)config.video_settings->handle(event, input.drawable_width, input.drawable_height);
        continue;
      }
      if (config.audio_settings && config.audio_settings->visible()) {
        (void)config.audio_settings->handle(event, input.drawable_width, input.drawable_height);
        continue;
      }
      const auto intent = workspace.handle(event, input.drawable_width,
                                           input.drawable_height, measure);
      switch (intent.kind) {
      case StartupIntentKind::Exit:
        exit = true;
        break;
      case StartupIntentKind::ReturnToCampaign:
        if (config.audio.confirm) config.audio.confirm();
        evidence.returned_to_campaign = true;
        window.set_text_input(false);
        return {{}, false, std::move(evidence), true};
      default:
        dispatch(intent);
        break;
      }
    }
    if (exit) {
      evidence.exit_requested = true;
      return {{}, true, std::move(evidence)};
    }
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
                     &portrait_provider, &artwork_provider);
    if (config.audio_settings)
      config.audio_settings->render(draw, input.drawable_width, input.drawable_height);
    if (config.video_settings)
      config.video_settings->render(draw, input.drawable_width, input.drawable_height);
    window.draw(draw);
  }
}
} // namespace stellar::native_startup_ui
