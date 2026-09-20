#include "native_new_game_workspace.hpp"
#include "native_menu_style.hpp"
#include <stellar/core/stellar_population_profiles.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <stellar/engine/native_ui_skin.hpp>
#include <cmath>
#include <iomanip>
#include <iterator>
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
  const float scale = std::clamp(static_cast<float>(height) / 1080.f, .8f, 2.5f);
  const float margin = 18.f * scale;
  const float available_width = std::max(1.f, static_cast<float>(width) - 2 * margin);
  const float available_height = std::max(1.f, std::min(820.f * scale, static_cast<float>(height) - 2 * margin));
  const float panel_width = std::min(1120.f * scale, available_width);
  UiRect panel_rect{(width - panel_width) * .5f, (height-available_height)*.5f, panel_width,
                    available_height};
  const float pad = 14.f * scale;
  const float x = panel_rect.x + pad, right = panel_rect.x + panel_rect.width - pad;
  const float header_h = 44.f * scale, mode_h = 58.f * scale;
  // Keep the player species as the first substantial decision on the page.
  // The galaxy population controls live beside the other generation settings
  // in the footer, where their selected values remain visible before Create.
  const float footer_h = 362.f * scale;
  UiRect heading{x, panel_rect.y + pad, right - x - 112.f * scale, header_h};
  UiRect cancel{right - 100.f * scale, panel_rect.y + pad, 100.f * scale,
                34.f * scale};
  const float gap = 10.f * scale;
  const float footer_y = panel_rect.y + panel_rect.height - pad - footer_h;
  UiRect story{x, footer_y, (right - x - gap) * .5f, mode_h};
  UiRect sandbox{story.x + story.width + gap, story.y, story.width, mode_h};
  const float content_y = heading.y + header_h + 10.f * scale;
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
  const float generation_y = footer_y + mode_h + 52.f * scale;
  UiRect seed_label{x, generation_y, seed_w, 22.f * scale};
  UiRect seed_input{x, generation_y + 24.f * scale, seed_w - 112.f * scale,
                    36.f * scale};
  UiRect randomize_seed{seed_input.x + seed_input.width + 6.f * scale,
                        seed_input.y, 106.f * scale, 36.f * scale};
  UiRect restore_defaults{seed_input.x, seed_input.y + 44.f * scale,
                          156.f * scale, 32.f * scale};
  UiRect create{right - 190.f * scale, footer_y + 314.f * scale,
                190.f * scale, 38.f * scale};
  UiRect sizes{randomize_seed.x + randomize_seed.width + gap, generation_y,
               right - create.width - gap -
                   (randomize_seed.x + randomize_seed.width + gap),
               146.f * scale};
  UiRect portrait{detail_content.x, detail_content.y, 116.f * scale,
                  116.f * scale};
  const float size_width = (sizes.width - 6.f * scale) * .5f;
  const float size_height = 26.f * scale;
  const float size_y = sizes.y + 18.f * scale;
  std::array<UiRect, 8> size_buttons{};
  for(std::size_t i=0;i<size_buttons.size();++i)size_buttons[i]={sizes.x+static_cast<float>(i%2)*(size_width+4.f*scale),
    size_y+static_cast<float>(i/2)*(size_height+4.f*scale),size_width,size_height};
  return {scale, static_cast<int>(24 * scale), std::max(14,static_cast<int>(17 * scale)),
          std::max(12,static_cast<int>(14 * scale)), panel_rect, heading, cancel, story,
          sandbox, species, rows, details, detail_content, sizes, seed_label,
          seed_input, randomize_seed, restore_defaults, create, portrait, size_buttons,{restore_defaults.x+restore_defaults.width+8*scale,restore_defaults.y,138*scale,restore_defaults.height},
          {story.x,story.y+mode_h+4*scale,story.width,32*scale},
          {sandbox.x,sandbox.y+mode_h+4*scale,sandbox.width,32*scale},
          {x,create.y-66*scale,(right-x)*.46f,26*scale},
          {x+(right-x)*.47f,create.y-66*scale,(right-x)*.46f,26*scale},
          {x,create.y-32*scale,(right-x)*.46f,26*scale},
          {x+(right-x)*.47f,create.y-32*scale,(right-x)*.46f,26*scale}};
}

void NativeNewGameWorkspace::set_view(NativeNewCampaignSetupView value) {
  const bool first_view = !view_;
  view_ = std::move(value);
  if(!view_->developer_mode){developer_research_={};developer_coverage_=false;developer_exploration_=false;}
  if (first_view) {
    selected_pre_warp_civilization_count_ =
        view_->default_pre_warp_civilization_count;
    selected_ancient_civilization_count_ =
        view_->default_ancient_civilization_count;
  }
  species_scroll_ = 0;
  detail_scroll_ = 0;
  message_.clear();
  reset_interaction();
  reconcile();
}
void NativeNewGameWorkspace::clear() noexcept { *this = {}; }
void NativeNewGameWorkspace::reset_interaction() noexcept {
  dropdown_.close();
  hover_feedback_.reset();
  seed_focused_ = false;
  pressed_ = false;
  pointer_ = {};
}
void NativeNewGameWorkspace::set_assessment_message(std::string message,
                                                     bool accepted) {
  message_ = std::move(message);
  assessment_accepted_ = accepted;
}
void NativeNewGameWorkspace::randomize_seed() {
  static std::atomic<std::uint64_t> sequence{};
  const auto now = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto entropy = now ^ (++sequence * 0x9e3779b97f4a7c15ULL);
  seed_text_ = std::to_string(static_cast<std::int64_t>(
      entropy & 0x7fff'ffff'ffff'ffffULL));
  seed_replace_pending_ = false;
  message_.clear();
}
void NativeNewGameWorkspace::restore_defaults() {
  population_={};requested_population_=stellar::core::PopulationSelection::Random;morphology_selected_=false;page_=SandboxPage::GalaxyType;developer_research_={};developer_coverage_=false;developer_exploration_=false;
  if (!view_) return;
  selected_species_id_ = view_->default_species_id;
  selected_system_count_ = view_->default_system_count;
  selected_pre_warp_civilization_count_ =
      view_->default_pre_warp_civilization_count;
  selected_ancient_civilization_count_ =
      view_->default_ancient_civilization_count;
  reconcile();
  randomize_seed();
  detail_scroll_ = 0;
  message_.clear();
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
  const auto select_count = [](const auto &choices, int current, int fallback) {
    if (std::ranges::any_of(choices, [=](const auto &choice) { return choice.count == current; })) return current;
    if (std::ranges::any_of(choices, [=](const auto &choice) { return choice.count == fallback; })) return fallback;
    const auto recommended = std::ranges::find(choices, true, &NativeCivilizationCountOption::recommended);
    return recommended != choices.end() ? recommended->count : choices.empty() ? 0 : choices.front().count;
  };
  selected_pre_warp_civilization_count_ = select_count(view_->pre_warp_civilization_presets, selected_pre_warp_civilization_count_, view_->default_pre_warp_civilization_count);
  selected_ancient_civilization_count_ = select_count(view_->ancient_civilization_presets, selected_ancient_civilization_count_, view_->default_ancient_civilization_count);
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
    add("PHYSIOLOGY",layout.small_font,6.f*layout.scale);
    add("Adult mass  "+number(option->adult_mass_kg,0)+" kg · Maturity  "+number(option->maturity_years,0)+" years",layout.body_font,4.f*layout.scale);
    add("Lifespan  "+number(option->lifespan_years,0)+" years · Metabolic demand  "+number(option->metabolic_demand,2)+"x Terran baseline",layout.body_font,4.f*layout.scale);
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

#include "native_galaxy_creation.inl"

NativeNewGameIntent NativeNewGameWorkspace::handle(const InputEvent &event,
                                                    int width, int height,
                                                    const TextMeasurer &measure) {
  if (!view_) return {};
  if(page_!=SandboxPage::Configuration)return handle_galaxy_page(event,width,height);
  const auto measured = measure_layout(width, height, measure);
  const auto &layout = measured.base;
  const std::array choice_bounds{layout.mode_story,layout.mode_sandbox,layout.morphology,layout.population};
  if(dropdown_.visible()){
    const int id=dropdown_.id();const auto anchor=choice_bounds[id];
    hover_feedback_.update(event,dropdown_.hover_target(event.position,anchor,width,height));
    if(const auto chosen_option=dropdown_.handle(event,anchor,width,height)){
      message_.clear();pressed_=false;
      if(id==0){selected_pre_warp_civilization_count_=view_->pre_warp_civilization_presets[*chosen_option].count;return {NativeNewGameIntentKind::SelectRivals,true,{},{},0,selected_pre_warp_civilization_count_,selected_ancient_civilization_count_};}
      if(id==1){selected_ancient_civilization_count_=view_->ancient_civilization_presets[*chosen_option].count;return {NativeNewGameIntentKind::SelectAncients,true,{},{},0,selected_pre_warp_civilization_count_,selected_ancient_civilization_count_};}
      if(id==2){population_.morphology=static_cast<stellar::core::GalaxyMorphology>(*chosen_option);population_.state=stellar::core::stellar_default_population_state(population_.morphology);}
      if(id==3)population_.state=static_cast<stellar::core::PopulationState>(*chosen_option);
    }
    return {NativeNewGameIntentKind::None,true};
  }
  auto target=stellar::native_menu_audio::hit(event.position,{layout.cancel,layout.seed_input,layout.randomize_seed,layout.restore_defaults,layout.copy_setup,layout.create,layout.mode_story,layout.mode_sandbox});
  if(const auto index=species_hit(event.position,measured))target=100+*index;
  if(const auto index=size_hit(event.position,layout))target=200+*index;
  hover_feedback_.update(event,pressed_?0:target);
  if (event.type == InputEventType::PointerMove) pointer_ = event.position;
  if (event.type == InputEventType::EscapePressed) {
    reset_interaction();
    page_=SandboxPage::Population;return {NativeNewGameIntentKind::None, true};
  }
  if (event.type == InputEventType::PointerCancelled) {
    reset_interaction();
    return {NativeNewGameIntentKind::None, true};
  }
  if (event.type == InputEventType::BackspacePressed && seed_focused_) {
    if (seed_replace_pending_) { seed_text_.clear(); seed_replace_pending_ = false; }
    erase_last_utf8(seed_text_);
    message_.clear();
    return {NativeNewGameIntentKind::SeedEdited, true, {}, seed_text_};
  }
  if (event.type == InputEventType::TextEntered && seed_focused_) {
    if (seed_replace_pending_) { seed_text_.clear(); seed_replace_pending_ = false; }
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
  if (seed_focused_) seed_replace_pending_ = true;
  if(view_->developer_mode){
    if(layout.developer_exploration.contains(event.position)){
      developer_exploration_=!developer_exploration_;return {NativeNewGameIntentKind::None,true};
    }
    if(layout.developer_coverage.contains(event.position)){
      developer_coverage_=!developer_coverage_;return {NativeNewGameIntentKind::None,true};
    }
    if(layout.developer_normal_research.contains(event.position)){
      developer_research_.complete_normal_research=!developer_research_.complete_normal_research;
      return {NativeNewGameIntentKind::None,true};
    }
    if(layout.developer_special_research.contains(event.position)){
      developer_research_.complete_special_research=!developer_research_.complete_special_research;
      return {NativeNewGameIntentKind::None,true};
    }
  }
  if (layout.cancel.contains(event.position)) {
    reset_interaction();
    page_=SandboxPage::Population;return {NativeNewGameIntentKind::None, true};
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
  if(layout.morphology.contains(event.position)){page_=SandboxPage::GalaxyType;reset_interaction();return {NativeNewGameIntentKind::None,true};}
  if(layout.population.contains(event.position)){page_=SandboxPage::Population;reset_interaction();return {NativeNewGameIntentKind::None,true};}
  for(int id=0;id<2;++id)if(choice_bounds[id].contains(event.position)){
    std::vector<std::string> options;int chosen_option{};
    if(id<2){const auto& choices=id==0?view_->pre_warp_civilization_presets:view_->ancient_civilization_presets;const int current=id==0?selected_pre_warp_civilization_count_:selected_ancient_civilization_count_;
      for(const auto& choice:choices){if(choice.count==current)chosen_option=static_cast<int>(options.size());options.push_back(choice.label);}}
    else if(id==2){for(int i=0;i<6;++i)options.emplace_back(stellar::core::morphology_name(static_cast<stellar::core::GalaxyMorphology>(i)));chosen_option=static_cast<int>(population_.morphology);}
    else {for(int i=0;i<5;++i)options.emplace_back(stellar::core::population_state_name(static_cast<stellar::core::PopulationState>(i)));chosen_option=static_cast<int>(population_.state);}
    dropdown_.open(id,std::move(options),chosen_option);pressed_=false;return {NativeNewGameIntentKind::None,true};
  }
  if (layout.randomize_seed.contains(event.position)) {
    randomize_seed();
    return {NativeNewGameIntentKind::RandomizeSeed, true, {}, seed_text_};
  }
  if (layout.restore_defaults.contains(event.position)) {
    restore_defaults();
    return {NativeNewGameIntentKind::RestoreDefaults, true, {}, seed_text_,
            selected_system_count_, selected_pre_warp_civilization_count_,
            selected_ancient_civilization_count_};
  }
  if(layout.copy_setup.contains(event.position))
    return {NativeNewGameIntentKind::CopySetup,true,selected_species_id_,seed_text_,selected_system_count_,selected_pre_warp_civilization_count_,selected_ancient_civilization_count_,stellar::core::StellarPopulationOptions{population_.morphology,stellar::core::resolve_population_state(generation_configuration()?generation_configuration()->base_seed:0,population_.morphology,requested_population_)},developer_research_,developer_coverage_,requested_population_,developer_exploration_};
  if (layout.create.contains(event.position))
    return {NativeNewGameIntentKind::Create, true, selected_species_id_,
            seed_text_, selected_system_count_, selected_pre_warp_civilization_count_,
            selected_ancient_civilization_count_,stellar::core::StellarPopulationOptions{population_.morphology,stellar::core::resolve_population_state(generation_configuration()?generation_configuration()->base_seed:0,population_.morphology,requested_population_)},developer_research_,developer_coverage_,requested_population_,developer_exploration_};
  return {NativeNewGameIntentKind::None, layout.panel.contains(event.position)};
}

void NativeNewGameWorkspace::render(
    DrawList &out, int width, int height,
    const TextMeasurer &measure,
    const PortraitProvider *portrait_provider,
    std::shared_ptr<const RgbaImage> backdrop) const {
  if (!view_) return;
  if(page_!=SandboxPage::Configuration){render_galaxy_page(out,width,height,portrait_provider,std::move(backdrop));return;}
  const bool has_backdrop = static_cast<bool>(backdrop);
  const Color panel_tint = has_backdrop ? Color{8, 20, 36, 200}
                                        : panel;
  const Color raised_tint = has_backdrop ? Color{12, 31, 54, 220}
                                         : raised;
  const Color selected_tint = has_backdrop ? Color{23, 67, 102, 235}
                                           : selected;
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
    fill(out, *visible, panel_tint);
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
  if (backdrop) {
    const float ratio = std::max(static_cast<float>(width) / backdrop->width(),
                                 static_cast<float>(height) / backdrop->height());
    const float image_width = backdrop->width() * ratio;
    const float image_height = backdrop->height() * ratio;
    out.overlay.emplace_back(Image{
        std::move(backdrop),
        {(width - image_width) * .5f, (height - image_height) * .5f,
         image_width, image_height},
        std::nullopt, {255, 255, 255, 255},
        UiRect{0, 0, static_cast<float>(width), static_cast<float>(height)}});
  } else {
    fill(out, {0, 0, static_cast<float>(width), static_cast<float>(height)},
         background);
  }
  native_menu_style::panel(out,layout.panel,layout.scale);
  text(out, layout.heading, "CONFIGURE SANDBOX", bright, layout.heading_font,
       TextAlign::Left, FontFace::Heading);
  fill(out, layout.cancel,
       layout.cancel.contains(pointer_) ? hover : raised_tint);
  stroke(out, layout.cancel, border);
  text(out, layout.cancel, "BACK", bright, layout.body_font,
       TextAlign::Center);

  for(const auto& [rect,label]:std::array<std::pair<UiRect,std::string>,2>{
      std::pair{layout.morphology,"SHAPE: "+(population_.morphology==stellar::core::GalaxyMorphology::BarredSpiral?std::string("Barred Spiral"):std::string(stellar::core::morphology_name(population_.morphology)))},
      std::pair{layout.population,"POPULATION: "+std::string(stellar::core::population_selection_label(requested_population_))}}){
    fill(out,rect,rect.contains(pointer_)?hover:raised_tint);stroke(out,rect,border);text(out,rect,label,bright,layout.small_font,TextAlign::Center);
  }
  const auto count_label = [](const auto &choices, const int count) {
    const auto found = std::ranges::find(choices, count,
                                         &NativeCivilizationCountOption::count);
    return found == choices.end() ? std::string{"Unavailable"} : found->label;
  };
  fill(out, layout.mode_story, layout.mode_story.contains(pointer_) ? hover : raised_tint);
  stroke(out, layout.mode_story, border);
  text(out, {layout.mode_story.x + 10 * s, layout.mode_story.y + 7 * s,
             layout.mode_story.width - 20 * s, 22 * s},
       "RIVAL EMPIRES", gold, layout.small_font);
  text(out, {layout.mode_story.x + 10 * s, layout.mode_story.y + 30 * s,
             layout.mode_story.width - 20 * s, 18 * s},
       count_label(view_->pre_warp_civilization_presets,
                   selected_pre_warp_civilization_count_), bright, layout.body_font);
  fill(out, layout.mode_sandbox, layout.mode_sandbox.contains(pointer_) ? hover : raised_tint);
  stroke(out, layout.mode_sandbox, border);
  text(out, {layout.mode_sandbox.x + 10 * s, layout.mode_sandbox.y + 7 * s,
             layout.mode_sandbox.width - 20 * s, 22 * s},
       "ANCIENT EMPIRES", gold, layout.small_font);
  text(out, {layout.mode_sandbox.x + 10 * s, layout.mode_sandbox.y + 30 * s,
             layout.mode_sandbox.width - 20 * s, 18 * s},
       count_label(view_->ancient_civilization_presets,
                   selected_ancient_civilization_count_), bright, layout.body_font);

  fill(out, layout.species, raised_tint);
  stroke(out, layout.species, border);
  text(out, {layout.species.x + 8 * s, layout.species.y + 8 * s,
             layout.species.width - 16 * s, 22 * s},
       "PLAYABLE SPECIES", gold, layout.small_font);
  for (std::size_t index = 0; index < view_->species.size(); ++index) {
    const auto row = measured.species_rows[index];
    const auto clip = intersection(row, layout.species_rows);
    if (!clip) continue;
    const bool chosen = view_->species[index].id == selected_species_id_;
    fill(out, *clip, chosen ? selected_tint
                           : row.contains(pointer_) ? hover : panel_tint);
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

  fill(out, layout.details, raised_tint);
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
    line("PHYSIOLOGY",gold,layout.small_font,6.f);
    line("Adult mass  "+number(species->adult_mass_kg,0)+" kg · Maturity  "+number(species->maturity_years,0)+" years");
    line("Lifespan  "+number(species->lifespan_years,0)+" years · Metabolic demand  "+number(species->metabolic_demand,2)+"x Terran baseline");
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
  fill(out, layout.seed_input,
       seed_focused_ ? selected_tint : raised_tint);
  stroke(out, layout.seed_input, seed_focused_ ? accent : border);
  text(out, {layout.seed_input.x + 9 * s, layout.seed_input.y + 8 * s,
             layout.seed_input.width - 18 * s, 22 * s},
       seed_text_.empty() ? "Enter a numeric seed" : seed_text_,
       seed_text_.empty() ? muted : bright, layout.body_font);
  fill(out, layout.randomize_seed,
       layout.randomize_seed.contains(pointer_) ? hover : raised_tint);
  stroke(out, layout.randomize_seed, border);
  text(out, layout.randomize_seed, "RANDOMIZE", bright, layout.small_font,
       TextAlign::Center);
  fill(out, layout.restore_defaults,
       layout.restore_defaults.contains(pointer_) ? hover : raised_tint);
  stroke(out, layout.restore_defaults, border);
  text(out, layout.restore_defaults, "RESTORE DEFAULTS", bright,
       layout.small_font, TextAlign::Center);

  if (!view_->size_presets.empty()) {
    const auto count =
        std::min(view_->size_presets.size(), layout.size_buttons.size());
    for (std::size_t index = 0; index < count; ++index) {
      const auto &size = view_->size_presets[index];
      const auto button = layout.size_buttons[index];
      fill(out, button,
           size.system_count == selected_system_count_ ? selected_tint
                                                       : raised_tint);
      stroke(out, button,
             size.system_count == selected_system_count_ ? accent : border);
      auto compact = size.label;
      if (const auto separator = compact.find(" - ");
          separator != std::string::npos)
        compact.replace(separator, 3, " · ");
      if(const auto units=compact.find(" systems");units!=std::string::npos)compact.erase(units);
      const UiRect caption{button.x+2*s,button.y+(button.height-layout.small_font*1.35f)*.5f,button.width-4*s,button.height};
      text(out, caption, std::move(compact), bright, layout.small_font,
           TextAlign::Center);
    }
    text(out, {layout.size_group.x, layout.size_group.y,
               layout.size_group.width, 18 * s},
         "GALAXY SIZE · SYSTEMS", gold, layout.small_font);
  }
  native_menu_style::button(out,layout.copy_setup,"COPY SETUP",layout.small_font,layout.copy_setup.contains(pointer_),true,s);
  if(view_->developer_mode){
    const auto checkbox=[&](UiRect row,bool checked,std::string caption){
      const UiRect box{row.x,row.y+3*s,20*s,20*s};
      fill(out,box,row.contains(pointer_)?hover:raised);stroke(out,box,checked?accent:border);
      if(checked)text(out,box,"✓",accent,layout.body_font,TextAlign::Center);
      text(out,{row.x+28*s,row.y+3*s,row.width-28*s,row.height},std::move(caption),bright,layout.small_font);
    };
    checkbox(layout.developer_normal_research,developer_research_.complete_normal_research,"All normal research completed");
    checkbox(layout.developer_special_research,developer_research_.complete_special_research,"Include special research");
    checkbox(layout.developer_coverage,developer_coverage_,"Full celestial coverage (QA only)");
    checkbox(layout.developer_exploration,developer_exploration_,"Entire galaxy explored and surveyed");
  }else text(out,{layout.seed_label.x,layout.create.y-32*s,layout.panel.width-28*s,26*s},
       generation_configuration()?"Resolved population: "+std::string(stellar::core::population_state_name(generation_configuration()->resolved_population))+" · "+std::to_string(selected_system_count_)+" systems":"Enter a valid seed to resolve the population",muted,layout.small_font);
  if(!view_->developer_mode)text(out,{layout.seed_label.x,layout.create.y+5*s,layout.create.x-layout.seed_label.x-10*s,25*s},
       view_->developer_mode?"DEV · Unchecked research follows normal progression":"Reproduction requires the same seed and generation settings.",gold,layout.small_font);
  const UiRect notice{layout.seed_label.x,layout.create.y+layout.create.height+5*s,layout.create.x-layout.seed_label.x-10*s,18*s};
  text(out, notice,
       message_.empty()
           ? std::to_string(std::max(0, selected_pre_warp_civilization_count_ - 1)) +
                 " rival empires · " +
                 std::to_string(selected_ancient_civilization_count_) +
                 " ancient empires"
           : message_,
       message_.empty() ? muted : assessment_accepted_ ? accent : warning,
       layout.small_font);
  fill(out, layout.create,
       layout.create.contains(pointer_) ? hover : selected_tint);
  stroke(out, layout.create, accent);
  text(out, layout.create, "CREATE CAMPAIGN", bright, layout.body_font,
       TextAlign::Center);
  const std::array choice_bounds{layout.mode_story,layout.mode_sandbox,layout.morphology,layout.population};
  for(const auto r:choice_bounds)text(out,{r.x+r.width-24*s,r.y+(r.height-layout.small_font)*.5f,20*s,24*s},"▼",accent,layout.small_font,TextAlign::Center);
  if(dropdown_.visible())dropdown_.render(out,choice_bounds[dropdown_.id()],width,height,layout.body_font);
}

} // namespace stellar::native_setup_ui
