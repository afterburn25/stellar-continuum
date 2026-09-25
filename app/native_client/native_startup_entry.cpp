#include "native_startup_entry.hpp"
#include "native_accessibility_bridge.hpp"
#include "native_audio_settings.hpp"
#include "native_pad_input.hpp"
#include "native_settings_hub.hpp"
#include "native_voice_settings.hpp"
#include "native_general_settings.hpp"
#include "native_video_controller.hpp"
#include "native_video_settings_smoke.hpp"
#include "native_audio_settings_smoke.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string_view>
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

std::filesystem::path startup_capture_path(
    const std::filesystem::path &setup_capture, std::string_view suffix) {
  auto filename = setup_capture.stem().native();
  const auto setup_suffix = std::filesystem::path{"-setup"}.native();
  if (filename.size() >= setup_suffix.size() &&
      filename.compare(filename.size() - setup_suffix.size(),
                       setup_suffix.size(), setup_suffix) == 0)
    filename.erase(filename.size() - setup_suffix.size());
  filename += std::filesystem::path{std::string(suffix)}.native();
  auto result = setup_capture;
  result.replace_filename(filename + setup_capture.extension().native());
  return result;
}
} // namespace

StartupEntryResult run_native_startup_entry(Window &window,
                                             StartupEntryConfig config,
                                             const StartupEntryAutomation *automation) {
  if (!config.utc_timestamp)
    throw std::invalid_argument("Startup requires a UTC timestamp provider.");
  if (config.minimum_boot_artwork < std::chrono::milliseconds::zero())
    throw std::invalid_argument("Startup artwork minimum duration cannot be negative.");
  if(automation&&automation->developer_mode){
    if(!config.developer_access||!config.developer_access->set_active(true))
      throw std::runtime_error("Developer smoke requires explicit launch eligibility.");
  }
  NativeStartupHost host(config.host);
  host.set_localization(config.locale);
  if(config.developer_access&&config.developer_access->active())host.set_developer_mode(true);
  NativeStartupWorkspace workspace;
  workspace.set_localization(config.locale);
  workspace.set_hover_callback(config.audio.hover);
  workspace.set_build_label("Stellar Continuum " + config.host.game_version);
  const std::string system_info="Stellar Continuum "+config.host.game_version+"\n"+window.graphics_adapter()+"\nDisplay: "+std::to_string(window.drawable_width())+" x "+std::to_string(window.drawable_height());
  workspace.set_diagnostics(window.graphics_adapter()+"\nDisplay: "+std::to_string(window.drawable_width())+" x "+std::to_string(window.drawable_height()));
  const auto route_settings = [&](const InputEvent& e,int w,int h) {
    const auto route=[&](auto*settings,const auto&label){
      const int focus_before=settings->focused();
      const auto snapshot=[&]([[maybe_unused]]std::optional<bool>&checked,
                              [[maybe_unused]]std::optional<stellar::engine::AnnouncementRange>&range){
        if constexpr(requires{settings->focused_toggle();})
          checked=settings->focused_toggle();
        if constexpr(requires{settings->focused_range();})
          range=settings->focused_range();
      };
      std::optional<bool> checked_before;std::optional<stellar::engine::AnnouncementRange> range_before;
      snapshot(checked_before,range_before);
      const bool captured=settings->handle(e,w,h);
      std::optional<bool> checked;std::optional<stellar::engine::AnnouncementRange> range;
      snapshot(checked,range);
      if(config.announcer&&(settings->focused()!=focus_before||checked!=checked_before||range!=range_before)){
        const auto rect=settings->focused_bounds(w,h);
        stellar::engine::AnnouncementControl control=
            stellar::engine::AnnouncementControl::Custom;
        if constexpr(requires{settings->focused_control();})
          control=settings->focused_control();
        config.announcer->announce_focus(label(),
            rect?std::optional<stellar::engine::AnnouncementBounds>{
                     {rect->x,rect->y,rect->width,rect->height}}
                :std::nullopt,range,control,checked);
      }
      return captured;
    };
    if(config.voice_settings&&config.voice_settings->visible())
      return route(config.voice_settings,[&]{return config.voice_settings->focused_label();});
    if(config.general_settings&&config.general_settings->visible())
      return route(config.general_settings,[&]{return config.general_settings->focused_label();});
    if(config.video_settings&&config.video_settings->visible())
      return route(config.video_settings,[&]{return config.video_settings->focused_label(w,h);});
    if(config.audio_settings&&config.audio_settings->visible())
      return route(config.audio_settings,[&]{return config.audio_settings->focused_label();});
    if(config.settings_hub)
      return route(config.settings_hub,[&]{return config.settings_hub->focused_label();});
    return false;
  };
  const auto saved_slots=host.slots();
  if(!saved_slots.slots.empty())workspace.set_continue_save(saved_slots.slots.front().path);
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
          entry->second = decode_rgba_image(config.asset_root / entry->first,0,stellar::native_map::ImageDecodeUsage::PixelsOnly);
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
                                     (config.locale&&config.locale->contains("STARTUP_LOADING_ASSETS")?std::string(config.locale->translate("STARTUP_LOADING_ASSETS")):std::string("LOADING GAME ASSETS")), {235,244,255,255},
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
    case StartupIntentKind::CopyDiagnostics:
      try{window.set_clipboard_text(system_info);workspace.set_diagnostics(system_info+"\nCopied to clipboard.");}
      catch(const std::exception&){workspace.set_diagnostics(system_info+"\nClipboard unavailable. Try again.");}
      break;
    case StartupIntentKind::CopySetup:
      try {
        NativeNewCampaignSetupController controller;
        controller.set_localization(config.locale);
        const auto prepared=controller.prepare({intent.seed_text,intent.system_count,intent.species_id,
          config.utc_timestamp(),intent.pre_warp_civilization_count,intent.ancient_civilization_count,
          intent.stellar_population,intent.developer_research,intent.developer_full_coverage,intent.requested_population,intent.developer_full_exploration},
          config.developer_access&&config.developer_access->active());
        if(!prepared.accepted)throw std::invalid_argument(prepared.message);
        std::string details=stellar::core::galaxy_configuration_description(*prepared.prepared->options().configuration);
        if(config.developer_access&&config.developer_access->active())details+="\nDeveloper session: yes\nAll normal research: "+std::string(intent.developer_research.complete_normal_research?"yes":"no")+"\nInclude special research: "+std::string(intent.developer_research.complete_special_research?"yes":"no")+"\nFull celestial coverage: "+std::string(intent.developer_full_coverage?"yes":"no")+"\nEntire galaxy explored and surveyed: "+std::string(intent.developer_full_exploration?"yes":"no");
        window.set_clipboard_text(details);
        workspace.set_setup_message("Setup and generation fingerprint copied.",true);
      }catch(const std::exception& error){workspace.set_setup_message(error.what(),false);}break;
    case StartupIntentKind::OpenSettings:
      if(config.settings_hub)config.settings_hub->open();
      else if (config.audio_settings) config.audio_settings->open();
      break;
    case StartupIntentKind::OpenLoad:
      workspace.set_slots(host.slots());
      break;
    case StartupIntentKind::Create: {
      const auto started = host.start_new(
          {intent.seed_text, intent.system_count, intent.species_id,
           config.utc_timestamp(), intent.pre_warp_civilization_count,
           intent.ancient_civilization_count,intent.stellar_population,intent.developer_research,intent.developer_full_coverage,intent.requested_population,intent.developer_full_exploration});
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
    if(workspace.screen()==StartupScreen::Entry){
      (void)workspace.handle({InputEventType::PointerMove,center(entry_layout.new_campaign)},width,height,measure);
      (void)workspace.handle({InputEventType::PointerMove,center(entry_layout.new_campaign)},width,height,measure);
    }
    if (!automation->audio_settings_screenshot.empty()) {
      if (!config.audio_settings) throw std::runtime_error("Audio settings validation requires the real overlay.");
      stellar::native_audio::check_audio_settings(*config.audio_settings,
          automation->audio_settings_path, width, height, "startup",
          [&] { dispatch(workspace.handle({InputEventType::LeftPressed, center(entry_layout.settings)}, width, height, measure));
            if(config.settings_hub)(void)route_settings({InputEventType::LeftPressed,center(stellar::native_settings::HubLayout::for_viewport(width,height).categories[1])},width,height); },
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
        if(!route_settings(event,width,height))dispatch(workspace.handle(event,width,height,measure));
      };
      stellar::native_video_settings::check_video_settings(*config.video_settings,
          automation->video_settings_path,width,height,"startup",
          [&]{if(config.settings_hub)config.settings_hub->close();
              route({InputEventType::LeftPressed,center(entry_layout.settings)});
              route({InputEventType::LeftPressed,center(config.settings_hub?stellar::native_settings::HubLayout::for_viewport(width,height).categories[2]:stellar::native_audio::AudioSettingsLayout::for_viewport(width,height).video)});},route,
          [&](bool confirming){DrawList draw;
            workspace.render(draw,width,height,measure,&portrait_provider,&artwork_provider);
            config.video_settings->render(draw,width,height);
            window.draw(draw,confirming?automation->video_confirm_screenshot:automation->video_settings_screenshot);
          });
    }
    if(config.settings_hub)config.settings_hub->close();
    if (automation->action != StartupEntryAutomationAction::Create) {
      evidence.setup_opened = workspace.screen() == StartupScreen::Setup;
      if (workspace.screen() == StartupScreen::Setup) {
        const auto back = workspace.handle({InputEventType::EscapePressed},
                                           width, height, measure);
        if (back.kind != StartupIntentKind::Back ||
            workspace.screen() != StartupScreen::ModeSelection)
          throw std::runtime_error(
              "Startup lifecycle automation could not return to game type selection.");
        const auto entry = workspace.handle({InputEventType::EscapePressed},
                                            width, height, measure);
        if (entry.kind != StartupIntentKind::Back ||
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
      DrawList menu_draw;
      workspace.render(menu_draw, width, height, measure, &portrait_provider,
                       &artwork_provider);
      window.draw(menu_draw, startup_capture_path(automation->setup_screenshot,
                                                  "-menu"));
      intent = workspace.handle(
          {InputEventType::LeftPressed, center(entry_layout.new_campaign)},
          width, height, measure);
      if (intent.kind != StartupIntentKind::OpenModeSelection ||
          workspace.screen() != StartupScreen::ModeSelection)
        throw std::runtime_error("Startup automation could not open game type selection.");
      DrawList modes_draw;
      workspace.render(modes_draw, width, height, measure, &portrait_provider,
                       &artwork_provider);
      window.draw(modes_draw, startup_capture_path(automation->setup_screenshot,
                                                   "-modes"));
      intent = workspace.handle(
          {InputEventType::LeftPressed, center(entry_layout.sandbox_campaign)},
          width, height, measure);
      evidence.setup_opened = intent.kind == StartupIntentKind::OpenSetup &&
                              workspace.screen() == StartupScreen::Setup;
    } else {
      evidence.setup_opened = workspace.screen() == StartupScreen::Setup;
      intent = {StartupIntentKind::OpenSetup, evidence.setup_opened};
    }
    const auto galaxy_layout=stellar::native_setup_ui::GalaxyChoiceLayout::for_viewport(width,height);
    DrawList types_draw;workspace.render(types_draw,width,height,measure,&portrait_provider,&artwork_provider);
    window.draw(types_draw,startup_capture_path(automation->setup_screenshot,"-galaxy-types"));
    (void)workspace.handle({InputEventType::LeftPressed,center(galaxy_layout.cards.at(automation->galaxy_card))},width,height,measure);
    DrawList selected_draw;workspace.render(selected_draw,width,height,measure,&portrait_provider,&artwork_provider);
    window.draw(selected_draw,startup_capture_path(automation->setup_screenshot,"-galaxy-selected"));
    (void)workspace.handle({InputEventType::LeftPressed,center(galaxy_layout.next)},width,height,measure);
    DrawList population_draw;workspace.render(population_draw,width,height,measure,&portrait_provider,&artwork_provider);
    window.draw(population_draw,startup_capture_path(automation->setup_screenshot,"-population"));
    (void)workspace.handle({InputEventType::LeftPressed,center(galaxy_layout.population)},width,height,measure);
    DrawList population_menu;workspace.render(population_menu,width,height,measure,&portrait_provider,&artwork_provider);
    window.draw(population_menu,startup_capture_path(automation->setup_screenshot,"-population-dropdown"));
    (void)workspace.handle({InputEventType::EscapePressed},width,height,measure);
    (void)workspace.handle({InputEventType::LeftPressed,center(galaxy_layout.next)},width,height,measure);
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
    if(automation->developer_mode&&automation->complete_normal_research)
      (void)workspace.handle({InputEventType::LeftPressed,center(measured.base.developer_normal_research)},width,height,measure);
    if(automation->developer_mode&&automation->full_celestial_coverage)
      (void)workspace.handle({InputEventType::LeftPressed,center(measured.base.developer_coverage)},width,height,measure);
    if(automation->full_exploration)
      (void)workspace.handle({InputEventType::LeftPressed,center(measured.base.developer_exploration)},width,height,measure);
    DrawList setup_draw;
    workspace.render(setup_draw, width, height, measure, &portrait_provider,
                     &artwork_provider);
    window.draw(setup_draw, automation->setup_screenshot);
    intent = workspace.handle(
        {InputEventType::LeftPressed, center(measured.base.create)}, width,
        height, measure);
    evidence.create_requested = intent.kind == StartupIntentKind::Create;
    if(automation->developer_mode&&intent.developer_research.complete_normal_research!=automation->complete_normal_research)
      throw std::runtime_error("Developer research checkbox did not reach campaign setup.");
    if(automation->developer_mode&&intent.developer_full_coverage!=automation->full_celestial_coverage)
      throw std::runtime_error("Developer coverage checkbox did not reach campaign setup.");
    if(intent.developer_full_exploration!=automation->full_exploration)
      throw std::runtime_error("Full exploration checkbox did not reach campaign setup.");
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
  stellar::native_client::PadNavigationRepeater pad_nav_repeater;
  stellar::native_client::PadStickNavigator pad_stick_nav;
  auto last_frame = std::chrono::steady_clock::now();
  for (;;) {
    if (config.audio.service) config.audio.service();
    const auto input = window.poll();
    const auto frame_now = std::chrono::steady_clock::now();
    const auto frame_dt = std::chrono::duration<float>(frame_now - last_frame).count();
    last_frame = frame_now;
    std::vector<InputEvent> pad_nav_events;
    if (!input.focused) {
      pad_nav_repeater.clear();
      pad_stick_nav.clear(pad_nav_repeater);
    }
    pad_nav_repeater.update(frame_dt, [&](const InputEvent &held) {
      if (!(config.settings_hub && config.settings_hub->capturing()))
        if (auto nav = stellar::native_client::pad_navigation_event(held))
          pad_nav_events.push_back(*nav);
    });
    std::vector<InputEvent> frame_events = pad_nav_events;
    // Assistive-tech Invoke calls queue on the bridge off-thread; each
    // drains as a Return press+release through normal dispatch.
    if (config.accessibility_bridge)
      for (auto n = config.accessibility_bridge->drain_activations(); n > 0; --n) {
        InputEvent press{InputEventType::KeyPressed}; press.key = 13;
        InputEvent release{InputEventType::KeyReleased}; release.key = 13;
        frame_events.push_back(press);
        frame_events.push_back(release);
      }
    // Queued RangeValue SetValue calls route to whichever visible settings
    // panel owns the focused slider.
    if (config.accessibility_bridge)
      if (const auto set_value = config.accessibility_bridge->take_range_set())
        (void)((config.audio_settings && config.audio_settings->visible() &&
                config.audio_settings->set_focused_range(*set_value)) ||
               (config.voice_settings && config.voice_settings->visible() &&
                config.voice_settings->set_focused_range(*set_value)));
    frame_events.insert(frame_events.end(), input.events.begin(), input.events.end());
    if (config.video_settings) config.video_settings->service(input.focused, input.renderable());
    if (input.quit_requested) {
      evidence.exit_requested = true;
      return {{}, true, std::move(evidence)};
    }
    const auto announce_focus=[&]{
      if(config.announcer){
        const auto rect=workspace.focused_bounds(input.drawable_width,
                                                 input.drawable_height,measure);
        config.announcer->announce_focus(
            workspace.focused_label(input.drawable_width,input.drawable_height,
                                    measure),
            rect?std::optional<stellar::engine::AnnouncementBounds>{
                     {rect->x,rect->y,rect->width,rect->height}}
                :std::nullopt,
            std::nullopt,
            workspace.focused_control(input.drawable_width,
                                      input.drawable_height,measure),
            std::nullopt,
            workspace.focused_value(input.drawable_width,
                                    input.drawable_height,measure));
      }
    };
    if (!input.renderable()) {
      for (const auto &raw : frame_events) {
        pad_nav_repeater.note(raw);
        const bool pad_nav_owned=
            !(config.settings_hub&&config.settings_hub->capturing());
        const auto stick_press=
            raw.type==InputEventType::GamepadAxis
                ?pad_stick_nav.note(raw,pad_nav_repeater,pad_nav_owned)
                :std::optional<InputEvent>{};
        const InputEvent event=[&]{
          if(stick_press)
            if(auto nav=stellar::native_client::pad_navigation_event(*stick_press))return *nav;
          if(raw.type==InputEventType::GamepadPressed&&pad_nav_owned)
            if(auto nav=stellar::native_client::pad_navigation_event(raw))return *nav;
          return raw;}();
        if (!route_settings(event,input.drawable_width,input.drawable_height)) {
          const int focus_before=workspace.focused();
          (void)workspace.handle(event, input.drawable_width,
                                    input.drawable_height, measure);
          if(workspace.focused()!=focus_before)announce_focus();
        }
      }
      window.set_text_input(workspace.wants_text_input()&&
          !(config.general_settings&&config.general_settings->visible())&&!(config.settings_hub&&config.settings_hub->visible()));
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      continue;
    }
    bool exit{};
    for (const auto &raw : frame_events) {
      pad_nav_repeater.note(raw);
      const bool pad_nav_owned=
          !(config.settings_hub&&config.settings_hub->capturing());
      const auto stick_press=
          raw.type==InputEventType::GamepadAxis
              ?pad_stick_nav.note(raw,pad_nav_repeater,pad_nav_owned)
              :std::optional<InputEvent>{};
      // Pad presses become navigation keys on the startup screens — no
      // gameplay context exists yet — except while a rebind capture in the
      // settings hub waits for the raw trigger.
      const InputEvent event=[&]{
        if(stick_press)
          if(auto nav=stellar::native_client::pad_navigation_event(*stick_press))return *nav;
        if(raw.type==InputEventType::GamepadPressed&&pad_nav_owned)
          if(auto nav=stellar::native_client::pad_navigation_event(raw))return *nav;
        return raw;}();
      if(is_developer_shortcut(event)){
        if(config.developer_access&&config.developer_access->eligible()&&workspace.screen()!=StartupScreen::Busy){
          const bool enabled=!config.developer_access->active();
          host.set_developer_mode(enabled);
          (void)config.developer_access->set_active(enabled);
          workspace.set_setup(host.setup());
          const auto slots=host.slots();
          workspace.set_continue_save(slots.slots.empty()?std::filesystem::path{}:slots.slots.front().path);
          workspace.set_slots(slots);workspace.show_entry();
          if(config.audio.confirm)config.audio.confirm();
        }
        continue;
      }
      if(route_settings(event,input.drawable_width,input.drawable_height))continue;
      if (config.general_settings && config.general_settings->visible()) {
        (void)config.general_settings->handle(event, input.drawable_width, input.drawable_height);
        continue;
      }
      if (config.video_settings && config.video_settings->visible()) {
        (void)config.video_settings->handle(event, input.drawable_width, input.drawable_height);
        continue;
      }
      if (config.audio_settings && config.audio_settings->visible()) {
        (void)config.audio_settings->handle(event, input.drawable_width, input.drawable_height);
        continue;
      }
      const int focus_before=workspace.focused();
      const auto intent = workspace.handle(event, input.drawable_width,
                                           input.drawable_height, measure);
      if(workspace.focused()!=focus_before)announce_focus();
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
        if (automation) throw std::runtime_error(state.status);
        std::cerr << state.status << '\n';
        workspace.show_failure(state.status);
      } else if (state.phase == NativeStartupPhase::Cancelled)
        workspace.show_entry();
      else
        workspace.set_operation(state);
    }
    window.set_text_input(workspace.wants_text_input()&&
        !(config.general_settings&&config.general_settings->visible())&&!(config.settings_hub&&config.settings_hub->visible()));
    DrawList draw;
    workspace.render(draw, input.drawable_width, input.drawable_height, measure,
                     &portrait_provider, &artwork_provider,
                     (config.settings_hub&&config.settings_hub->visible())||
                     (config.audio_settings&&config.audio_settings->visible())||
                     (config.video_settings&&config.video_settings->visible())||
                     (config.general_settings&&config.general_settings->visible())||
                     (config.voice_settings&&config.voice_settings->visible()));
    if (config.audio_settings)
      config.audio_settings->render(draw, input.drawable_width, input.drawable_height);
    if (config.video_settings)
      config.video_settings->render(draw, input.drawable_width, input.drawable_height);
    if (config.general_settings)
      config.general_settings->render(draw, input.drawable_width, input.drawable_height);
    if(config.caption)config.caption(draw,input.drawable_width,input.drawable_height);
    if(config.voice_settings)config.voice_settings->render(draw,input.drawable_width,input.drawable_height);
    if(config.settings_hub)config.settings_hub->render(draw,input.drawable_width,input.drawable_height);
    window.draw(draw);
  }
}
} // namespace stellar::native_startup_ui
