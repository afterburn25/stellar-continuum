#include "native_new_game_workspace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_setup_ui {
namespace {
using namespace stellar::native_map;
using namespace stellar::native_setup;

constexpr Color background{3, 9, 18, 255};
constexpr Color panel{8, 20, 36, 252};
constexpr Color raised{12, 31, 54, 250};
constexpr Color hover{24, 61, 94, 252};
constexpr Color selected{23, 67, 102, 255};
constexpr Color border{91, 151, 205, 235};
constexpr Color bright{235, 244, 255, 255};
constexpr Color muted{154, 181, 211, 240};
constexpr Color accent{122, 230, 190, 255};
constexpr Color gold{241, 195, 105, 255};
constexpr Color warning{255, 190, 112, 255};

[[nodiscard]] std::optional<UiRect> intersection(UiRect a, UiRect b) noexcept {
  const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  const float right = std::min(a.x + a.width, b.x + b.width);
  const float bottom = std::min(a.y + a.height, b.y + b.height);
  if (right <= x || bottom <= y) return std::nullopt;
  return UiRect{x, y, right - x, bottom - y};
}

void fill(DrawList &out, UiRect rect, Color color) {
  out.overlay.emplace_back(FilledRectangle{rect, color});
}
void stroke(DrawList &out, UiRect rect, Color color) {
  out.overlay.emplace_back(StrokedRectangle{rect, color});
}
void text(DrawList &out, UiRect rect, std::string value, Color color, int size,
          TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface,
          std::optional<UiRect> clip = std::nullopt) {
  const auto x = align == TextAlign::Left
                     ? rect.x
                     : align == TextAlign::Center ? rect.x + rect.width * .5f
                                                   : rect.x + rect.width;
  out.overlay.emplace_back(Text{{x, rect.y}, std::move(value), color, size,
                                rect.width, clip.value_or(rect), align, face});
}
void clipped_text(DrawList &out, UiRect rect, UiRect clip, std::string value,
                  Color color, int size, TextAlign align = TextAlign::Left) {
  const auto visible = intersection(rect, clip);
  if (visible) text(out, rect, std::move(value), color, size, align,
                    FontFace::Interface, *visible);
}
[[nodiscard]] std::string number(double value, int decimals) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(decimals) << value;
  return out.str();
}
[[nodiscard]] std::string band(const stellar::core::ToleranceBand &value,
                               std::string_view unit, int decimals) {
  return number(std::max(0., value.preferred - value.comfortable_deviation),
                decimals) +
         "–" + number(value.preferred + value.comfortable_deviation, decimals) +
         " " + std::string(unit);
}
void erase_last_utf8(std::string &value) {
  if (value.empty()) return;
  auto index = value.size() - 1;
  while (index > 0 &&
         (static_cast<unsigned char>(value[index]) & 0xc0u) == 0x80u)
    --index;
  value.erase(index);
}
[[nodiscard]] const NativeSpeciesSetupOption *selected_species(
    const NativeNewCampaignSetupView &view, std::string_view id) noexcept {
  const auto found = std::ranges::find(view.species, id,
                                       &NativeSpeciesSetupOption::id);
  return found == view.species.end() ? nullptr : &*found;
}
} // namespace

std::optional<NativeSpeciesPresentation>
species_presentation(std::string_view id) noexcept {
  if (id == "terran_baseline")
    return NativeSpeciesPresentation{
        "An oxygen-breathing, water-based people shaped for open terrestrial worlds.",
        "assets/visual/species/terran-baseline.jpg"};
  if (id == "pelagic_high_pressure")
    return NativeSpeciesPresentation{
        "Aquatic, water-based people whose free-swimming lives depend on pressure and buoyancy.",
        "assets/visual/species/pelagic-high-pressure.jpg"};
  if (id == "compact_high_gravity")
    return NativeSpeciesPresentation{
        "Dense, water-based terrestrial people adapted to high gravity and oxygen-rich air.",
        "assets/visual/species/compact-high-gravity.jpg"};
  if (id == "cryogenic_hydrocarbon")
    return NativeSpeciesPresentation{
        "Hydrocarbon-based terrestrial people whose slow lives suit cold, reducing worlds.",
        "assets/visual/species/cryogenic-hydrocarbon.jpg"};
  return std::nullopt;
}

NativeNewGameLayout NativeNewGameLayout::for_viewport(int width,
                                                       int height) noexcept {
  const float scale = std::clamp(static_cast<float>(height) / 900.f, 1.f, 2.4f);
  const float margin = 18.f * scale;
  const float available_width = std::max(1.f, static_cast<float>(width) - 2 * margin);
  const float available_height = std::max(1.f, static_cast<float>(height) - 2 * margin);
  const float panel_width = std::min(1120.f * scale, available_width);
  UiRect panel_rect{(width - panel_width) * .5f, margin, panel_width,
                    available_height};
  const float pad = 14.f * scale;
  const float x = panel_rect.x + pad, right = panel_rect.x + panel_rect.width - pad;
  const float header_h = 44.f * scale, mode_h = 58.f * scale;
  const float footer_h = 124.f * scale;
  UiRect heading{x, panel_rect.y + pad, right - x - 112.f * scale, header_h};
  UiRect cancel{right - 100.f * scale, panel_rect.y + pad, 100.f * scale,
                34.f * scale};
  const float mode_y = heading.y + header_h + 4.f * scale;
  const float gap = 10.f * scale;
  UiRect story{x, mode_y, (right - x - gap) * .5f, mode_h};
  UiRect sandbox{story.x + story.width + gap, mode_y, story.width, mode_h};
  const float content_y = mode_y + mode_h + 10.f * scale;
  const float footer_y = panel_rect.y + panel_rect.height - pad - footer_h;
  const float content_h = std::max(90.f, footer_y - content_y - gap);
  const float list_width = std::clamp(panel_width * .30f, 225.f * scale,
                                      310.f * scale);
  UiRect species{x, content_y, list_width, content_h};
  UiRect rows{species.x + 8.f * scale, species.y + 36.f * scale,
              species.width - 16.f * scale, species.height - 44.f * scale};
  UiRect details{species.x + species.width + gap, content_y,
                 right - (species.x + species.width + gap), content_h};
  UiRect detail_content{details.x + 12.f * scale, details.y + 12.f * scale,
                        details.width - 24.f * scale,
                        details.height - 24.f * scale};
  const float seed_w = std::max(220.f * scale, (right - x) * .42f);
  UiRect seed_label{x, footer_y, seed_w, 22.f * scale};
  UiRect seed_input{x, footer_y + 24.f * scale, seed_w, 36.f * scale};
  UiRect create{right - 190.f * scale, footer_y + 78.f * scale,
                190.f * scale, 38.f * scale};
  UiRect sizes{seed_input.x + seed_input.width + gap, footer_y,
               right - create.width - gap -
                   (seed_input.x + seed_input.width + gap),
               86.f * scale};
  UiRect portrait{detail_content.x, detail_content.y, 116.f * scale,
                  116.f * scale};
  const float size_width = (sizes.width - 4.f * scale) * .5f;
  const float size_height = 30.f * scale;
  const float size_y = sizes.y + 18.f * scale;
  std::array<UiRect, 4> size_buttons{
      UiRect{sizes.x, size_y, size_width, size_height},
      UiRect{sizes.x + size_width + 4.f * scale, size_y, size_width,
             size_height},
      UiRect{sizes.x, size_y + size_height + 4.f * scale, size_width,
             size_height},
      UiRect{sizes.x + size_width + 4.f * scale,
             size_y + size_height + 4.f * scale, size_width, size_height}};
  return {scale, static_cast<int>(24 * scale), static_cast<int>(15 * scale),
          static_cast<int>(12 * scale), panel_rect, heading, cancel, story,
          sandbox, species, rows, details, detail_content, sizes, seed_label,
          seed_input, create, portrait, size_buttons};
}

void NativeNewGameWorkspace::set_view(NativeNewCampaignSetupView value) {
  view_ = std::move(value);
  species_scroll_ = 0;
  detail_scroll_ = 0;
  message_.clear();
  reset_interaction();
  reconcile();
}
void NativeNewGameWorkspace::clear() noexcept { *this = {}; }
void NativeNewGameWorkspace::reset_interaction() noexcept {
  seed_focused_ = false;
  pressed_ = false;
  pointer_ = {};
}
void NativeNewGameWorkspace::set_assessment_message(std::string message,
                                                     bool accepted) {
  message_ = std::move(message);
  assessment_accepted_ = accepted;
}
void NativeNewGameWorkspace::reconcile() {
  if (!view_) return;
  if (!selected_species(*view_, selected_species_id_))
    selected_species_id_ = selected_species(*view_, view_->default_species_id)
                               ? view_->default_species_id
                               : view_->species.empty() ? std::string{}
                                                        : view_->species.front().id;
  if (std::ranges::none_of(view_->size_presets, [&](const auto &size) {
        return size.system_count == selected_system_count_;
      }))
    if (std::ranges::any_of(view_->size_presets, [&](const auto &size) {
          return size.system_count == view_->default_system_count;
        }))
      selected_system_count_ = view_->default_system_count;
    else if (const auto recommended =
                 std::ranges::find(view_->size_presets, true,
                                   &NativeGalaxySizeOption::recommended);
             recommended != view_->size_presets.end())
      selected_system_count_ = recommended->system_count;
    else
      selected_system_count_ = view_->size_presets.empty()
                                   ? 0
                                   : view_->size_presets.front().system_count;
}

NativeNewGameMeasuredLayout NativeNewGameWorkspace::measure_layout(
    int width, int height, const TextMeasurer &measure) const {
  NativeNewGameMeasuredLayout result;
  result.base = NativeNewGameLayout::for_viewport(width, height);
  if (!view_) return result;
  const auto &layout = result.base;
  const auto measured_height = [&](std::string value, int pixels,
                                   float wrap_width) {
    const auto extent = measure(Text{{0, 0}, std::move(value), bright, pixels,
                                     wrap_width});
    return static_cast<float>(std::max(extent.height, pixels));
  };
  float y = layout.species_rows.y - species_scroll_;
  for (const auto &option : view_->species) {
    const float label_height = measured_height(
        option.display_name, layout.body_font,
        layout.species_rows.width - 64.f * layout.scale);
    const float row_height = std::max(44.f * layout.scale,
                                      label_height + 16.f * layout.scale);
    result.species_rows.push_back(
        {layout.species_rows.x, y, layout.species_rows.width, row_height});
    y += row_height + 4.f * layout.scale;
    result.species_content_height += row_height + 4.f * layout.scale;
  }
  if (const auto *option = selected_species(*view_, selected_species_id_)) {
    const float identity_width = layout.details_content.width -
                                 layout.portrait.width - 12.f * layout.scale;
    const auto presentation = species_presentation(option->id);
    const float title_height = measured_height(option->display_name,
                                               layout.heading_font,
                                               identity_width);
    const float biography_height = presentation
        ? measured_height(std::string(presentation->biography),
                          layout.body_font, identity_width)
        : 0.f;
    result.details_header_height =
        std::max(layout.portrait.height,
                 title_height + biography_height + 8.f * layout.scale) +
        12.f * layout.scale;
    float content{};
    auto add = [&](std::string value, int pixels, float gap) {
      content += measured_height(std::move(value), pixels,
                                 layout.details_content.width -
                                     8.f * layout.scale) +
                 gap;
    };
    add("HOMEWORLD ENVIRONMENT TOLERANCES", layout.small_font, 6.f * layout.scale);
    add("Comfortable gravity  " + band(option->gravity_g, "g", 2),
        layout.body_font, 4.f * layout.scale);
    add("Survival gravity  " +
            band({option->gravity_g.preferred,
                  option->gravity_g.survivable_deviation,
                  option->gravity_g.survivable_deviation},
                 "g", 2),
        layout.body_font, 4.f * layout.scale);
    add("Comfortable temperature  " +
            band(option->temperature_kelvin, "K", 0),
        layout.body_font, 4.f * layout.scale);
    add("Survival temperature  " +
            band({option->temperature_kelvin.preferred,
                  option->temperature_kelvin.survivable_deviation,
                  option->temperature_kelvin.survivable_deviation},
                 "K", 0),
        layout.body_font, 4.f * layout.scale);
    add("Comfortable pressure  " + band(option->pressure_kpa, "kPa", 0),
        layout.body_font, 4.f * layout.scale);
    add("Survival pressure  " +
            band({option->pressure_kpa.preferred,
                  option->pressure_kpa.survivable_deviation,
                  option->pressure_kpa.survivable_deviation},
                 "kPa", 0),
        layout.body_font, 8.f * layout.scale);
    add("CHEMISTRY AND HABITAT", layout.small_font, 6.f * layout.scale);
    for (const auto &value :
         {"Biochemistry  " + option->biochemistry_label,
          "Preferred atmosphere  " + option->preferred_atmosphere_label,
          "Biological solvent  " + option->biological_solvent_label,
          "Radiation tolerance  " +
              number(option->radiation_tolerance * 100., 0) + "/100",
          option->requires_immersion
              ? std::string{"Requires an immersed workspace."}
              : option->can_operate_in_vacuum_unprotected
                    ? std::string{"Can operate in vacuum without protection."}
                    : std::string{"Requires protection in vacuum."}})
      add(value, layout.body_font, 4.f * layout.scale);
    result.details_content_height = content;
  }
  return result;
}

std::optional<std::size_t> NativeNewGameWorkspace::species_hit(
    Point point, const NativeNewGameMeasuredLayout &layout) const noexcept {
  if (!view_ || !layout.base.species_rows.contains(point)) return std::nullopt;
  for (std::size_t index = 0; index < layout.species_rows.size(); ++index)
    if (layout.species_rows[index].contains(point)) return index;
  return std::nullopt;
}
std::optional<std::size_t> NativeNewGameWorkspace::size_hit(
    Point point, const NativeNewGameLayout &layout) const noexcept {
  if (!view_) return std::nullopt;
  const auto count = std::min(view_->size_presets.size(), layout.size_buttons.size());
  for (std::size_t index = 0; index < count; ++index)
    if (layout.size_buttons[index].contains(point)) return index;
  return std::nullopt;
}

NativeNewGameIntent NativeNewGameWorkspace::handle(const InputEvent &event,
                                                    int width, int height,
                                                    const TextMeasurer &measure) {
  if (!view_) return {};
  const auto measured = measure_layout(width, height, measure);
  const auto &layout = measured.base;
  if (event.type == InputEventType::PointerMove) pointer_ = event.position;
  if (event.type == InputEventType::EscapePressed) {
    reset_interaction();
    return {NativeNewGameIntentKind::Cancel, true};
  }
  if (event.type == InputEventType::PointerCancelled) {
    reset_interaction();
    return {NativeNewGameIntentKind::None, true};
  }
  if (event.type == InputEventType::BackspacePressed && seed_focused_) {
    erase_last_utf8(seed_text_);
    message_.clear();
    return {NativeNewGameIntentKind::SeedEdited, true, {}, seed_text_};
  }
  if (event.type == InputEventType::TextEntered && seed_focused_) {
    if (seed_text_.size() + event.text.size() <= 80) seed_text_ += event.text;
    message_.clear();
    return {NativeNewGameIntentKind::SeedEdited, true, {}, seed_text_};
  }
  if (event.type == InputEventType::Wheel) {
    if (layout.species.contains(event.position)) {
      const float maximum = std::max(
          0.f, measured.species_content_height - layout.species_rows.height);
      species_scroll_ = std::clamp(species_scroll_ - event.wheel_y * 42.f *
                                   layout.scale, 0.f, maximum);
      return {NativeNewGameIntentKind::None, true};
    }
    if (layout.details.contains(event.position)) {
      const float maximum = std::max(
          0.f, measured.details_content_height -
                   std::max(1.f, layout.details_content.height -
                                     measured.details_header_height));
      detail_scroll_ = std::clamp(detail_scroll_ - event.wheel_y * 42.f *
                                  layout.scale, 0.f, maximum);
      return {NativeNewGameIntentKind::None, true};
    }
  }
  if (event.type == InputEventType::LeftReleased) {
    pressed_ = false;
    return {NativeNewGameIntentKind::None, layout.panel.contains(event.position)};
  }
  if (event.type != InputEventType::LeftPressed)
    return {NativeNewGameIntentKind::None, layout.panel.contains(event.position)};
  pressed_ = true;
  pointer_ = event.position;
  seed_focused_ = layout.seed_input.contains(event.position);
  if (layout.cancel.contains(event.position)) {
    reset_interaction();
    return {NativeNewGameIntentKind::Cancel, true};
  }
  if (const auto index = species_hit(event.position, measured)) {
    selected_species_id_ = view_->species[*index].id;
    detail_scroll_ = 0;
    message_.clear();
    return {NativeNewGameIntentKind::SelectSpecies, true,
            selected_species_id_};
  }
  if (const auto index = size_hit(event.position, layout)) {
    selected_system_count_ = view_->size_presets[*index].system_count;
    message_.clear();
    return {NativeNewGameIntentKind::SelectSize, true, {}, {},
            selected_system_count_};
  }
  if (layout.create.contains(event.position))
    return {NativeNewGameIntentKind::Create, true, selected_species_id_,
            seed_text_, selected_system_count_};
  return {NativeNewGameIntentKind::None, layout.panel.contains(event.position)};
}

void NativeNewGameWorkspace::render(
    DrawList &out, int width, int height,
    const TextMeasurer &measure,
    const PortraitProvider *portrait_provider) const {
  if (!view_) return;
  const auto measured = measure_layout(width, height, measure);
  const auto &layout = measured.base;
  const auto s = layout.scale;
  const auto measured_height = [&](std::string value, int pixels,
                                   float wrap_width) {
    const auto extent = measure(Text{{0, 0}, std::move(value), bright, pixels,
                                     wrap_width});
    return static_cast<float>(std::max(extent.height, pixels));
  };
  const auto portrait_image = [&](UiRect frame, UiRect clipping,
                                  std::string_view path) {
    const auto visible = intersection(frame, clipping);
    if (!visible) return;
    fill(out, *visible, panel);
    if (portrait_provider) {
      if (const auto image = (*portrait_provider)(path)) {
        const float ratio = std::min(
            frame.width / static_cast<float>(image->width()),
            frame.height / static_cast<float>(image->height()));
        const UiRect destination{
            frame.x + (frame.width - image->width() * ratio) * .5f,
            frame.y + (frame.height - image->height() * ratio) * .5f,
            image->width() * ratio, image->height() * ratio};
        out.overlay.emplace_back(Image{image, destination, std::nullopt,
                                       {255, 255, 255, 255}, *visible});
      }
    }
    stroke(out, *visible, border);
  };
  fill(out, {0, 0, static_cast<float>(width), static_cast<float>(height)},
       background);
  fill(out, layout.panel, panel);
  stroke(out, layout.panel, border);
  text(out, layout.heading, "CONFIGURE SANDBOX", bright, layout.heading_font,
       TextAlign::Left, FontFace::Heading);
  fill(out, layout.cancel,
       layout.cancel.contains(pointer_) ? hover : raised);
  stroke(out, layout.cancel, border);
  text(out, layout.cancel, "CANCEL", bright, layout.body_font,
       TextAlign::Center);

  fill(out, layout.mode_story, raised);
  stroke(out, layout.mode_story, muted);
  text(out, {layout.mode_story.x + 10 * s, layout.mode_story.y + 7 * s,
             layout.mode_story.width - 20 * s, 22 * s},
       "STORY CAMPAIGN", muted, layout.body_font);
  text(out, {layout.mode_story.x + 10 * s, layout.mode_story.y + 30 * s,
             layout.mode_story.width - 20 * s, 18 * s},
       "COMING SOON", gold, layout.small_font);
  fill(out, layout.mode_sandbox, selected);
  stroke(out, layout.mode_sandbox, accent);
  text(out, {layout.mode_sandbox.x + 10 * s, layout.mode_sandbox.y + 7 * s,
             layout.mode_sandbox.width - 20 * s, 22 * s},
       "SANDBOX", bright, layout.body_font);
  text(out, {layout.mode_sandbox.x + 10 * s, layout.mode_sandbox.y + 30 * s,
             layout.mode_sandbox.width - 20 * s, 18 * s},
       "Configure a reproducible galaxy", accent, layout.small_font);

  fill(out, layout.species, raised);
  stroke(out, layout.species, border);
  text(out, {layout.species.x + 8 * s, layout.species.y + 8 * s,
             layout.species.width - 16 * s, 22 * s},
       "PLAYABLE SPECIES", gold, layout.small_font);
  for (std::size_t index = 0; index < view_->species.size(); ++index) {
    const auto row = measured.species_rows[index];
    const auto clip = intersection(row, layout.species_rows);
    if (!clip) continue;
    const bool chosen = view_->species[index].id == selected_species_id_;
    fill(out, *clip, chosen ? selected
                           : row.contains(pointer_) ? hover : panel);
    if (chosen) stroke(out, *clip, accent);
    const UiRect thumbnail{row.x + 5.f * s, row.y + 5.f * s, 38.f * s,
                           row.height - 10.f * s};
    if (const auto presentation =
            species_presentation(view_->species[index].id))
      portrait_image(thumbnail, layout.species_rows,
                     presentation->portrait_asset_path);
    clipped_text(out, {row.x + 50 * s, row.y + 8 * s,
                       row.width - 56 * s, row.height - 16 * s},
                 layout.species_rows, view_->species[index].display_name,
                 chosen ? accent : bright, layout.body_font);
  }

  fill(out, layout.details, raised);
  stroke(out, layout.details, border);
  if (const auto *species = selected_species(*view_, selected_species_id_)) {
    const auto clip = layout.details_content;
    const auto presentation = species_presentation(species->id);
    auto portrait = layout.portrait;
    if (presentation)
      portrait_image(portrait, clip, presentation->portrait_asset_path);
    const float identity_x = portrait.x + portrait.width + 12.f * s;
    const float identity_width = clip.x + clip.width - identity_x;
    const float title_height = measured_height(
        species->display_name, layout.heading_font, identity_width);
    clipped_text(out, {identity_x, clip.y, identity_width,
                       title_height}, clip, species->display_name, accent,
                 layout.heading_font);
    float biography_height{};
    if (presentation) {
      biography_height = measured_height(std::string(presentation->biography),
                                          layout.body_font, identity_width);
      clipped_text(out,
                   {identity_x,
                    clip.y + title_height + 8.f * s,
                    identity_width, biography_height},
                   clip, std::string(presentation->biography), muted,
                   layout.body_font);
    }
    const UiRect facts_clip{clip.x, clip.y + measured.details_header_height,
                            clip.width,
                            std::max(1.f, clip.height -
                                             measured.details_header_height)};
    float y = facts_clip.y - detail_scroll_;
    auto line = [&](std::string value, Color color = bright,
                    int font = 0, float gap = 4.f) {
      const int pixels = font == 0 ? layout.body_font : font;
      const float height_line = measured_height(value, pixels, facts_clip.width);
      clipped_text(out, {facts_clip.x, y, facts_clip.width - 8.f * s,
                         height_line}, facts_clip,
                   std::move(value), color, pixels);
      y += height_line + gap * s;
    };
    line("HOMEWORLD ENVIRONMENT TOLERANCES", gold, layout.small_font, 6.f);
    line("Comfortable gravity  " + band(species->gravity_g, "g", 2));
    line("Survival gravity  " +
         band({species->gravity_g.preferred,
               species->gravity_g.survivable_deviation,
               species->gravity_g.survivable_deviation},
              "g", 2));
    line("Comfortable temperature  " +
         band(species->temperature_kelvin, "K", 0));
    line("Survival temperature  " +
         band({species->temperature_kelvin.preferred,
               species->temperature_kelvin.survivable_deviation,
               species->temperature_kelvin.survivable_deviation},
              "K", 0));
    line("Comfortable pressure  " + band(species->pressure_kpa, "kPa", 0));
    line("Survival pressure  " +
             band({species->pressure_kpa.preferred,
                   species->pressure_kpa.survivable_deviation,
                   species->pressure_kpa.survivable_deviation},
                  "kPa", 0),
         bright, 0, 8.f);
    line("CHEMISTRY AND HABITAT", gold, layout.small_font, 6.f);
    line("Biochemistry  " + species->biochemistry_label);
    line("Preferred atmosphere  " + species->preferred_atmosphere_label);
    line("Biological solvent  " + species->biological_solvent_label);
    line("Radiation tolerance  " +
         number(species->radiation_tolerance * 100., 0) + "/100");
    if (species->requires_immersion)
      line("Requires an immersed workspace.", warning);
    line(species->can_operate_in_vacuum_unprotected
             ? "Can operate in vacuum without protection."
             : "Requires protection in vacuum.",
         muted);
    const float maximum_scroll =
        std::max(0.f, measured.details_content_height - facts_clip.height);
    if (maximum_scroll > 0.f) {
      const UiRect track{facts_clip.x + facts_clip.width - 3.f * s,
                         facts_clip.y, 2.f * s, facts_clip.height};
      fill(out, track, {91, 151, 205, 80});
      const float handle_height =
          std::max(22.f * s, facts_clip.height * facts_clip.height /
                                   measured.details_content_height);
      const float handle_y =
          track.y + (track.height - handle_height) *
                        (detail_scroll_ / maximum_scroll);
      fill(out, {track.x, handle_y, track.width, handle_height}, accent);
      if (detail_scroll_ + .5f < maximum_scroll) {
        const UiRect fade{facts_clip.x, facts_clip.y + facts_clip.height - 24.f * s,
                          facts_clip.width - 6.f * s, 24.f * s};
        fill(out, fade, {8, 20, 36, 235});
        text(out, {fade.x, fade.y + 4.f * s, fade.width - 6.f * s,
                   17.f * s},
             "SCROLL FOR MORE", muted, layout.small_font, TextAlign::Right);
      }
    }
  }

  text(out, layout.seed_label, "GALAXY SEED", gold, layout.small_font);
  fill(out, layout.seed_input, seed_focused_ ? selected : raised);
  stroke(out, layout.seed_input, seed_focused_ ? accent : border);
  text(out, {layout.seed_input.x + 9 * s, layout.seed_input.y + 8 * s,
             layout.seed_input.width - 18 * s, 22 * s},
       seed_text_.empty() ? "Enter a numeric seed" : seed_text_,
       seed_text_.empty() ? muted : bright, layout.body_font);

  if (!view_->size_presets.empty()) {
    const auto count =
        std::min(view_->size_presets.size(), layout.size_buttons.size());
    for (std::size_t index = 0; index < count; ++index) {
      const auto &size = view_->size_presets[index];
      const auto button = layout.size_buttons[index];
      fill(out, button,
           size.system_count == selected_system_count_ ? selected : raised);
      stroke(out, button,
             size.system_count == selected_system_count_ ? accent : border);
      auto compact = size.label;
      if (const auto separator = compact.find(" - ");
          separator != std::string::npos)
        compact.replace(separator, 3, "\n");
      text(out, button, std::move(compact), bright, layout.small_font,
           TextAlign::Center);
    }
    text(out, {layout.size_group.x, layout.size_group.y,
               layout.size_group.width, 18 * s},
         "GALAXY SIZE", gold, layout.small_font);
  }
  const UiRect notice{layout.seed_input.x, layout.seed_input.y + 42 * s,
                      layout.create.x - layout.seed_input.x - 8 * s, 38 * s};
  text(out, notice,
       message_.empty()
           ? std::to_string(view_->fixed_pre_warp_civilization_count) +
                 " pre-warp civilizations · " +
                 std::to_string(view_->fixed_ancient_civilization_count) +
                 " ancient civilization"
           : message_,
       message_.empty() ? muted : assessment_accepted_ ? accent : warning,
       layout.small_font);
  fill(out, layout.create,
       layout.create.contains(pointer_) ? hover : selected);
  stroke(out, layout.create, accent);
  text(out, layout.create, "CREATE CAMPAIGN", bright, layout.body_font,
       TextAlign::Center);
}

} // namespace stellar::native_setup_ui
