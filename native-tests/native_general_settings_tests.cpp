#include "native_general_settings.hpp"
#include "native_ui_layout.hpp"
#include <stellar/engine/localization.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <variant>

namespace {
using namespace stellar::native_general;
using namespace stellar::native_map;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("stellar-general-settings-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
  }
  ~TempDirectory() {
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(), error);
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    if (!error && normalized.parent_path() == root &&
        normalized.filename().string().starts_with("stellar-general-settings-test-"))
      std::filesystem::remove_all(normalized, error);
  }
};

void write_raw(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "could not write settings fixture");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(static_cast<bool>(output), "could not finish settings fixture");
}

void empty_default_and_unicode_round_trip(const TempDirectory& temp) {
  const auto file = temp.path / "general-settings.json";
  NativeGeneralSettings defaults(file);
  require(defaults.saved().screenshot_directory.empty() && defaults.error().empty(),
          "missing settings did not choose the empty default folder");
  const auto unicode = temp.path / std::filesystem::path(u8"screenshots-测试-雪");
  std::filesystem::create_directories(unicode);
  require(defaults.save({unicode}), "could not save a valid Unicode screenshot folder");
  NativeGeneralSettings reloaded(file);
  require(reloaded.saved().screenshot_directory == unicode && reloaded.error().empty(),
          "schema 1 Unicode screenshot folder did not round-trip");
  std::ifstream input(file, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(input)), {});
  input.close();
  require(json.find("\"schemaVersion\":1") != std::string::npos,
          "schema version was not persisted");
  require(reloaded.saved().nebula_density==1,"Nebula preference did not default to Medium");
  auto navigator=reloaded.saved();navigator.asset_categories_collapsed={true,false,true,false,true};navigator.assets_hidden=true;navigator.nebula_density=2;navigator.reduce_motion=true;
  require(reloaded.save(navigator),"Navigator preferences failed to save");
  NativeGeneralSettings navigator_reload(file);
  require(navigator_reload.saved()==navigator,"Category collapse/hide preferences did not round-trip with screenshot path");
}

void invalid_files_use_default(const TempDirectory& temp) {
  const auto file = temp.path / "invalid-settings.json";
  const auto relative = std::filesystem::path("relative-folder").generic_string();
  const auto regular_file = temp.path / "not-a-folder.txt";
  write_raw(regular_file, "file");
  const auto non_directory = regular_file.generic_string();
  const auto nul_path = (temp.path / "valid-prefix").generic_string() + "\\u0000suffix";
  const std::vector<std::string> invalid = {
      "{not json",
      R"({"schemaVersion":2,"screenshotDirectory":""})",
      R"({"schemaVersion":1,"schemaVersion":1,"screenshotDirectory":""})",
      "{\"schemaVersion\":1,\"screenshotDirectory\":\"" + relative + "\"}",
      "{\"schemaVersion\":1,\"screenshotDirectory\":\"" + non_directory + "\"}",
      "{\"schemaVersion\":1,\"screenshotDirectory\":\"" + nul_path + "\"}",
      R"({"schemaVersion":1,"screenshotDirectory":"","assetCategoriesCollapsed":[false,true,false,true,false],"assetsHidden":false,"nebulaDensity":1,"eruptionQuality":2,"reduceMotion":1})",
      R"({"schemaVersion":1,"screenshotDirectory":"","interfaceScale":4})",
      std::string(4097, 'x')};
  for (const auto& contents : invalid) {
    write_raw(file, contents);
    NativeGeneralSettings settings(file);
    require(settings.saved().screenshot_directory.empty() && !settings.error().empty(),
            "invalid persisted screenshot folder did not fall back to default");
  }
}

void rejected_saves_retain_saved_preference(const TempDirectory& temp) {
  const auto file = temp.path / "atomic-settings.json";
  const auto first = temp.path / "first-folder";
  const auto second = temp.path / "second-folder";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);
  NativeGeneralSettings settings(file);
  require(settings.save({first}), "initial screenshot folder was not saved");
  require(!settings.save({std::filesystem::path("relative-folder")}) &&
              settings.saved().screenshot_directory == first,
          "relative save changed the prior screenshot preference");
  std::wstring nul_name = first.wstring();
  nul_name.push_back(L'\0');
  nul_name += L"trailing-name";
  require(!settings.save({std::filesystem::path(nul_name)}) &&
              settings.saved().screenshot_directory == first,
          "embedded NUL save changed the prior screenshot preference");

  std::error_code error;
  std::filesystem::remove(file, error);
  require(std::filesystem::create_directory(file, error),
          "could not prepare an atomic-write failure target");
  require(!settings.save({second}) && settings.saved().screenshot_directory == first &&
              !settings.error().empty(),
          "failed atomic write replaced the prior screenshot preference");
}

void click_button(NativeGeneralSettings& settings, UiRect rect, std::string_view name) {
  InputEvent event{};
  event.type = InputEventType::LeftPressed;
  event.position = {rect.x + rect.width * .5f, rect.y + rect.height * .5f};
  require(settings.handle(event, 1280, 720), std::string("visible general settings did not capture ") + std::string(name));
}

bool overlaps(UiRect left, UiRect right) {
  return left.x < right.x + right.width && right.x < left.x + left.width &&
         left.y < right.y + right.height && right.y < left.y + left.height;
}

std::size_t codepoint_count(std::string_view text) {
  std::size_t count{};
  for (const auto byte : text)
    if ((static_cast<unsigned char>(byte) & 0xc0u) != 0x80u) ++count;
  return count;
}

bool valid_utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    std::size_t length{};
    std::uint32_t value{};
    if (first <= 0x7f) { length = 1; value = first; }
    else if (first >= 0xc2 && first <= 0xdf) { length = 2; value = first & 0x1fu; }
    else if (first >= 0xe0 && first <= 0xef) { length = 3; value = first & 0x0fu; }
    else if (first >= 0xf0 && first <= 0xf4) { length = 4; value = first & 0x07u; }
    else return false;
    if (i + length > text.size()) return false;
    for (std::size_t j = 1; j < length; ++j) {
      const auto next = static_cast<unsigned char>(text[i + j]);
      if ((next & 0xc0u) != 0x80u) return false;
      value = (value << 6u) | (next & 0x3fu);
    }
    if ((length == 2 && value < 0x80u) || (length == 3 && value < 0x800u) ||
        (length == 4 && value < 0x10000u) || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) return false;
    i += length;
  }
  return true;
}

const Text& find_path_label(const DrawList& draw, UiRect folder) {
  for (const auto& command : draw.overlay) {
    const auto* text = std::get_if<Text>(&command);
    if (text && text->clip && text->clip->x > folder.x &&
        text->clip->y > folder.y && text->clip->width < folder.width &&
        text->clip->height < folder.height && text->clip->height > 40.f)
      return *text;
  }
  throw std::runtime_error("rendered screenshot path label was not found");
}

const Text& find_text_label(const DrawList& draw, std::string_view value) {
  for (const auto& command : draw.overlay) {
    const auto* text = std::get_if<Text>(&command);
    if (text && text->value == value) return *text;
  }
  throw std::runtime_error("rendered General Settings button was not found");
}

void layouts_fit_and_keep_controls_separate() {
  for (const auto [width, height] : {std::pair{1280, 720}, {1920, 1080},
                                     {2560, 1440}, {3840, 2160}}) {
    const auto layout = GeneralSettingsLayout::for_viewport(width, height);
    const auto inside = [&](UiRect rect) {
      return rect.width > 0.f && rect.height > 0.f && rect.x >= 0.f && rect.y >= 0.f &&
             rect.x + rect.width <= static_cast<float>(width) &&
             rect.y + rect.height <= static_cast<float>(height);
    };
    for (const auto rect : {layout.panel, layout.audio, layout.video, layout.folder,
                            layout.status, layout.browse, layout.defaults,
                            layout.cancel, layout.save, layout.motion})
      require(inside(rect), "General Settings layout escaped the viewport");
    require(!overlaps(layout.audio, layout.video), "audio and video navigation overlap");
    require(!overlaps(layout.folder, layout.status), "folder path and status areas overlap");
    require(!overlaps(layout.motion, layout.nebula) && !overlaps(layout.motion, layout.eruptions) &&
                !overlaps(layout.motion, layout.folder) && !overlaps(layout.motion, layout.status),
            "reduced motion control overlaps another General Settings region");
    const std::array<UiRect, 4> actions{layout.browse, layout.defaults,
                                        layout.cancel, layout.save};
    for (std::size_t i = 0; i < actions.size(); ++i) {
      require(!overlaps(layout.folder, actions[i]) && !overlaps(layout.status, actions[i]),
              "folder or status text overlaps an action button");
      for (std::size_t j = i + 1; j < actions.size(); ++j)
        require(!overlaps(actions[i], actions[j]), "General Settings action buttons overlap");
    }
  }
}

void picker_save_cancel_and_default_flow(const TempDirectory& temp) {
  const auto file = temp.path / "ui-settings.json";
  const auto first = temp.path / "ui-first";
  const auto second = temp.path / "ui-second";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);
  NativeGeneralSettings settings(file);
  require(settings.save({first}), "could not seed UI preference");

  const auto layout = GeneralSettingsLayout::for_viewport(1280, 720);
  int browse_calls{};
  std::uint64_t request_id{};
  std::vector<GeneralPreferences> applied;
  settings.set_apply([&](const auto& preference) { applied.push_back(preference); });
  settings.set_browse([&](std::uint64_t id, const auto&) {
    ++browse_calls;
    request_id = id;
    return true;
  });
  settings.open();
  click_button(settings, layout.browse, "initial Browse");
  require(browse_calls == 1 && settings.browsing(), "Browse button did not request the asynchronous picker");
  const auto first_request = request_id;
  InputEvent focus_lost{};
  focus_lost.type = InputEventType::PointerCancelled;
  require(settings.handle(focus_lost, 1280, 720) && settings.visible() && settings.browsing(),
          "focus loss dismissed the General Settings modal or its picker request");
  click_button(settings, layout.browse, "repeated Browse");
  require(browse_calls == 1 && request_id == first_request,
          "repeated Browse click opened a second picker while one was pending");
  settings.accept_browse_result({first_request, second, {}});
  require(!settings.browsing() && settings.draft().screenshot_directory == second &&
              settings.saved().screenshot_directory == first,
          "picker result changed saved preference before Save or did not update its draft");
  click_button(settings, layout.save, "Save");
  require(!settings.visible() && settings.saved().screenshot_directory == second &&
              NativeGeneralSettings(file).saved().screenshot_directory == second &&
              applied.size() == 1 && applied.back().screenshot_directory == second,
          "Save did not persist, apply and close the selected screenshot folder");

  settings.open();
  click_button(settings, layout.browse, "Browse before picker cancel");
  const auto cancelled_request = request_id;
  settings.accept_browse_result({cancelled_request, std::nullopt, {}});
  require(!settings.browsing() && settings.draft().screenshot_directory == second,
          "canceling the native picker changed its draft");
  click_button(settings, layout.cancel, "Cancel after picker cancel");
  require(!settings.visible() && settings.saved().screenshot_directory == second,
          "Cancel changed the saved folder");

  settings.open();
  click_button(settings, layout.browse, "Browse before picker error");
  settings.accept_browse_result({request_id, std::nullopt, "picker service unavailable"});
  require(!settings.browsing() && settings.visible() && !settings.error().empty() &&
              settings.saved().screenshot_directory == second,
          "picker error closed the modal or changed the saved folder");
  click_button(settings, layout.cancel, "Cancel after picker error");

  settings.open();
  click_button(settings, layout.browse, "Browse before close");
  const auto stale_request = request_id;
  click_button(settings, layout.cancel, "close pending Browse");
  settings.open();
  click_button(settings, layout.browse, "Browse after reopen");
  const auto current_request = request_id;
  require(current_request != stale_request, "reopened picker reused a stale request identifier");
  settings.accept_browse_result({stale_request, first, {}});
  require(settings.browsing() && settings.draft().screenshot_directory == second,
          "a stale picker result changed the new draft");
  settings.accept_browse_result({current_request, std::filesystem::path("relative-folder"), {}});
  require(!settings.browsing() && !settings.error().empty() &&
              settings.draft().screenshot_directory == second,
          "invalid picker result did not report an error and retain the previous draft");
  click_button(settings, layout.cancel, "Cancel invalid selection");
  settings.open();
  click_button(settings, layout.defaults, "Use Default draft");
  require(settings.draft().screenshot_directory.empty() &&
              settings.saved().screenshot_directory == second,
          "Use Default did not change only the draft");
  click_button(settings, layout.cancel, "Cancel default draft");
  require(settings.saved().screenshot_directory == second,
          "canceling a default-folder draft changed the saved setting");

  settings.open();
  click_button(settings, layout.defaults, "Use Default before Save");
  click_button(settings, layout.save, "Save default folder");
  require(!settings.visible() && settings.saved().screenshot_directory.empty() &&
              NativeGeneralSettings(file).saved().screenshot_directory.empty() &&
              applied.size() == 2 && applied.back().screenshot_directory.empty(),
          "Use Default followed by Save did not persist the default location");
}

void navigation_cancels_and_closed_view_ignores_events(const TempDirectory& temp) {
  const auto first = temp.path / "navigation-first";
  const auto second = temp.path / "navigation-second";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);
  const auto layout = GeneralSettingsLayout::for_viewport(1280, 720);
  for (const bool choose_video : {false, true}) {
    NativeGeneralSettings settings(temp.path / (choose_video ? "navigation-video.json" : "navigation-audio.json"));
    require(settings.save({first}), "could not seed navigation preference");
    int audio_calls{}, video_calls{};
    std::uint64_t request_id{};
    settings.set_navigation([&] { ++audio_calls; }, [&] { ++video_calls; });
    settings.set_browse([&](std::uint64_t id, const auto&) { request_id = id; return true; });
    settings.open();
    InputEvent outside{};
    outside.type = InputEventType::LeftPressed;
    outside.position = {-20.f, -20.f};
    require(settings.handle(outside, 1280, 720) && settings.visible(),
            "visible General Settings did not absorb an outside click");
    InputEvent key{};
    key.type = InputEventType::KeyPressed;
    key.key = 0x41;
    require(settings.handle(key, 1280, 720) && settings.visible(),
            "visible General Settings did not absorb an unrelated key");
    click_button(settings, layout.browse, "Browse before navigation");
    settings.accept_browse_result({request_id, second, {}});
    click_button(settings, choose_video ? layout.video : layout.audio,
                 choose_video ? "Video navigation" : "Audio navigation");
    require(!settings.visible() && settings.saved().screenshot_directory == first &&
                audio_calls == (choose_video ? 0 : 1) && video_calls == (choose_video ? 1 : 0),
            "navigation did not cancel the draft and invoke only its destination");
    require(!settings.handle(outside, 1280, 720) && !settings.handle(key, 1280, 720),
            "closed General Settings captured input");
  }
}

void failed_save_keeps_draft_and_does_not_apply(const TempDirectory& temp) {
  const auto file = temp.path / "failed-ui-save.json";
  const auto first = temp.path / "failed-ui-first";
  const auto second = temp.path / "failed-ui-second";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);
  NativeGeneralSettings settings(file);
  require(settings.save({first}), "could not seed failed-save preference");
  int apply_calls{};
  settings.set_apply([&](const auto&) { ++apply_calls; });
  std::error_code error;
  std::filesystem::remove(file, error);
  require(std::filesystem::create_directory(file, error), "could not prepare UI save failure");
  std::uint64_t request_id{};
  settings.set_browse([&](std::uint64_t id, const auto&) { request_id = id; return true; });
  settings.open();
  const auto layout = GeneralSettingsLayout::for_viewport(1280, 720);
  click_button(settings, layout.browse, "Browse before failed Save");
  settings.accept_browse_result({request_id, second, {}});
  click_button(settings, layout.save, "failed Save");
  require(settings.visible() && settings.draft().screenshot_directory == second &&
              settings.saved().screenshot_directory == first && !settings.error().empty() &&
              apply_calls == 0,
          "failed Save closed the panel, lost the draft, changed saved settings, or applied it");
}

void long_unicode_path_wrap_cache_and_scroll(const TempDirectory& temp) {
  auto path = temp.path / "nested";
  const std::array<std::u8string_view, 3> segments{
      u8"层级-1-abcdefghijk", u8"层级-2-lmnopqrstuv",
      u8"层级-3-wxyzabcdef"};
  for (const auto segment : segments) path /= std::filesystem::path(std::u8string(segment));
  std::filesystem::create_directories(path);
  NativeGeneralSettings settings(temp.path / "long-path-settings.json");
  require(settings.save({path}), "could not save the long Unicode screenshot path");
  std::size_t wrapped_height_measurements{};
  std::size_t measurement_calls{};
  std::string last_measurement;
  settings.set_text_measurer([&](const Text& text) {
    ++measurement_calls;
    last_measurement = text.value;
    if (text.value.find('\n') != std::string::npos) ++wrapped_height_measurements;
    const auto lines = static_cast<int>(1 + std::count(text.value.begin(), text.value.end(), '\n'));
    return TextExtent{static_cast<int>(codepoint_count(text.value) * 22u), lines * 18};
  });
  settings.open();
  constexpr int width = 1280, height = 720;
  const auto layout = GeneralSettingsLayout::for_viewport(width, height);
  DrawList first_draw;
  settings.render(first_draw, width, height);
  const auto& path_label = find_path_label(first_draw, layout.folder);
  const std::string rendered = path_label.value;
  const auto encoded_path = path.u8string();
  const std::string original(reinterpret_cast<const char*>(encoded_path.data()), encoded_path.size());
  std::string rebuilt;
  std::size_t line_start{};
  while (line_start <= rendered.size()) {
    const auto line_end = rendered.find('\n', line_start);
    const auto line = rendered.substr(line_start, line_end == std::string::npos
        ? std::string::npos : line_end - line_start);
    require(valid_utf8(line), "wrapped screenshot path split a UTF-8 code point");
    rebuilt += line;
    if (line_end == std::string::npos) break;
    line_start = line_end + 1;
  }
  require(rebuilt == original, "wrapped screenshot path lost or changed characters");
  require(path_label.clip && path_label.clip->x >= layout.folder.x &&
              path_label.clip->y >= layout.folder.y &&
              path_label.clip->x + path_label.clip->width <= layout.folder.x + layout.folder.width &&
              path_label.clip->y + path_label.clip->height <= layout.folder.y + layout.folder.height,
          "wrapped screenshot path escaped its folder clip");
  require(wrapped_height_measurements == 1,
          "first path render did not perform exactly one whole-path height measurement");

  DrawList second_draw;
  const auto measurements_after_first = measurement_calls;
  settings.render(second_draw, width, height);
  require(find_path_label(second_draw, layout.folder).value == rendered &&
              wrapped_height_measurements == 2 &&
              measurement_calls == measurements_after_first + 1 &&
              last_measurement == rendered,
          "unchanged path render recomputed wrapping instead of measuring height only");
  const auto save_before = find_text_label(second_draw, "SAVE").at;
  const auto path_y_before = find_path_label(second_draw, layout.folder).at.y;
  InputEvent wheel{};
  wheel.type = InputEventType::Wheel;
  wheel.position = {layout.folder.x + layout.folder.width * .5f,
                    layout.folder.y + layout.folder.height * .5f};
  wheel.wheel_y = -1.f;
  require(settings.handle(wheel, width, height), "path-area wheel was not captured");
  DrawList scrolled_draw;
  settings.render(scrolled_draw, width, height);
  require(find_path_label(scrolled_draw, layout.folder).at.y < path_y_before &&
              find_text_label(scrolled_draw, "SAVE").at.x == save_before.x &&
              find_text_label(scrolled_draw, "SAVE").at.y == save_before.y,
          "wheel scrolling did not move only the path text");
}
} // namespace

int main() {
  try {
    TempDirectory temp;
    empty_default_and_unicode_round_trip(temp);
    {
      NativeGeneralSettings density(temp.path/"nebula-visual.json");density.open();const auto l=GeneralSettingsLayout::for_viewport(1280,720);
      click_button(density,l.nebula,"nebula dropdown");
      InputEvent end{InputEventType::KeyPressed};end.key=0x4000004du;(void)density.handle(end,1280,720);
      InputEvent accept{InputEventType::KeyPressed};accept.key=13;(void)density.handle(accept,1280,720);
      require(density.draft().nebula_density==2&&density.saved().nebula_density==1,"Dropdown selection did not stay in draft");
      click_button(density,l.save,"save nebula density");NativeGeneralSettings reloaded(temp.path/"nebula-visual.json");require(reloaded.saved().nebula_density==2,"Selected nebula visual density was not saved");
      reloaded.open();click_button(reloaded,l.nebula,"nebula dropdown");InputEvent home{InputEventType::KeyPressed};home.key=0x4000004au;(void)reloaded.handle(home,1280,720);(void)reloaded.handle(accept,1280,720);reloaded.cancel();require(reloaded.saved().nebula_density==2,"Cancel changed nebula density");
    }
    {
      NativeGeneralSettings motion(temp.path/"motion.json");const auto l=GeneralSettingsLayout::for_viewport(1280,720);
      motion.open();click_button(motion,l.motion,"reduced motion toggle");
      require(motion.draft().reduce_motion&&!motion.saved().reduce_motion,"Reduced motion click did not stay in draft");
      click_button(motion,l.save,"save reduced motion");
      NativeGeneralSettings reloaded(temp.path/"motion.json");
      require(reloaded.saved().reduce_motion,"Reduced motion preference did not persist");
      reloaded.open();click_button(reloaded,l.motion,"reduced motion off");reloaded.cancel();
      require(reloaded.saved().reduce_motion,"Cancel changed reduced motion");
      DrawList draw;reloaded.open();reloaded.render(draw,1280,720);
      require(find_text_label(draw,"Reduced motion (decorative animation): On").value.size()>0,"Reduced motion state was not rendered");
    }
    {
      NativeGeneralSettings scale(temp.path/"iscale.json");const auto l=GeneralSettingsLayout::for_viewport(1280,720);
      scale.open();click_button(scale,l.iscale,"interface scale cycle");
      require(scale.draft().interface_scale==2&&scale.saved().interface_scale==1,"Interface scale click did not stay in draft");
      click_button(scale,l.save,"save interface scale");
      NativeGeneralSettings reloaded(temp.path/"iscale.json");
      require(reloaded.saved().interface_scale==2,"Interface scale preference did not persist");
      reloaded.open();click_button(reloaded,l.iscale,"interface scale cycle");click_button(reloaded,l.iscale,"interface scale wrap");reloaded.cancel();
      require(reloaded.saved().interface_scale==2,"Cancel changed interface scale");
      require(interface_scale_multiplier(0)<1.f&&interface_scale_multiplier(1)==1.f&&interface_scale_multiplier(2)>1.f,"Interface scale multipliers are not ordered");
      const auto base=NativeUiLayout::for_viewport(1920,1080).scale;
      NativeUiLayout::set_user_scale(interface_scale_multiplier(2));
      const auto enlarged=NativeUiLayout::for_viewport(1920,1080).scale;
      NativeUiLayout::set_user_scale(1.f);
      require(enlarged>base,"Interface scale preference did not enlarge UI layout");
      DrawList draw;reloaded.open();reloaded.render(draw,1280,720);
      require(find_text_label(draw,"Interface scale: Large").value.size()>0,"Interface scale state was not rendered");
    }
    {
      NativeGeneralSettings flash(temp.path/"flash.json");const auto l=GeneralSettingsLayout::for_viewport(1280,720);
      flash.open();click_button(flash,l.flashing,"reduce flashing toggle");
      require(flash.draft().reduce_flashing&&!flash.saved().reduce_flashing,"Reduce flashing click did not stay in draft");
      click_button(flash,l.save,"save reduce flashing");
      NativeGeneralSettings reloaded(temp.path/"flash.json");
      require(reloaded.saved().reduce_flashing,"Reduce flashing preference did not persist");
      DrawList draw;reloaded.open();reloaded.render(draw,1280,720);
      require(find_text_label(draw,"Reduce flashing: On").value.size()>0,"Reduce flashing state was not rendered");
    }
    {
      // Localization: loaded keys override literals; missing keys fall back.
      stellar::engine::LocalizationTable locale{"en","en"};
      std::string lerr;
      require(locale.load_json(R"({"locale":"en","strings":{"SETTINGS_GENERAL_TITLE":"TEST TITLE","SETTINGS_STATE_OFF":"DISABLED","SETTINGS_REDUCE_MOTION":"Motion: {0}","SETTINGS_NEBULA_DENSITY":"Nebula {0} ▾"}})",&lerr),("Locale JSON rejected: "+lerr).c_str());
      NativeGeneralSettings localized(temp.path/"localized.json");
      localized.set_localization(&locale);
      localized.open();
      DrawList draw;localized.render(draw,1280,720);
      require(find_text_label(draw,"TEST TITLE").value=="TEST TITLE","Localized title was not rendered");
      require(find_text_label(draw,"Motion: DISABLED").value=="Motion: DISABLED","Localized format substitution failed");
      require(find_text_label(draw,"Nebula Medium ▾").value=="Nebula Medium ▾","Nested localized quality name failed");
      require(find_text_label(draw,"SAVE").value=="SAVE","Missing key did not fall back to the literal");
      require(find_text_label(draw,"SCREENSHOT FOLDER").value=="SCREENSHOT FOLDER","Unlisted label did not fall back to the literal");
    }
    invalid_files_use_default(temp);
    rejected_saves_retain_saved_preference(temp);
    picker_save_cancel_and_default_flow(temp);
    layouts_fit_and_keep_controls_separate();
    navigation_cancels_and_closed_view_ignores_events(temp);
    failed_save_keeps_draft_and_does_not_apply(temp);
    long_unicode_path_wrap_cache_and_scroll(temp);
  } catch (const std::exception& error) {
    std::cerr << "native general settings tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native general settings tests passed\n";
  return 0;
}
