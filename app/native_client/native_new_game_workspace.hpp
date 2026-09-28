#pragma once
#include "native_menu_hover.hpp"
#include "native_dropdown.hpp"

#include "native_new_campaign_setup.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine { class LocalizationTable; }
namespace stellar::native_setup_ui {

struct NativeSpeciesPresentation {
  std::string_view biography;
  std::string_view portrait_asset_path;
};

[[nodiscard]] std::optional<NativeSpeciesPresentation>
species_presentation(std::string_view species_id) noexcept;

enum class SandboxPage { GalaxyType, Population, Configuration };
struct GalaxyChoiceLayout {
  float scale{};stellar::native_map::UiRect panel,heading,back,next,preview,population,description,summary;
  std::array<stellar::native_map::UiRect,6> cards;
  static GalaxyChoiceLayout for_viewport(int width,int height) noexcept;
};
struct NativeNewGameLayout {
  float scale{};
  int heading_font{}, body_font{}, small_font{};
  stellar::native_map::UiRect panel, heading, cancel, mode_story, mode_sandbox,
      species, species_rows, details, details_content, size_group, seed_label,
      seed_input, randomize_seed, restore_defaults, create, portrait;
  std::array<stellar::native_map::UiRect, 8> size_buttons{};
  stellar::native_map::UiRect copy_setup;
  stellar::native_map::UiRect morphology, population;
  stellar::native_map::UiRect developer_normal_research,developer_special_research,developer_coverage,developer_exploration;
  [[nodiscard]] static NativeNewGameLayout for_viewport(int width,
                                                         int height) noexcept;
};

struct NativeNewGameMeasuredLayout {
  NativeNewGameLayout base;
  std::vector<stellar::native_map::UiRect> species_rows;
  float species_content_height{}, details_header_height{},
      details_content_height{};
};

enum class NativeNewGameIntentKind {
  None,
  Cancel,
  SelectSpecies,
  SelectSize,
  SelectRivals,
  SelectAncients,
  SeedEdited,
  RandomizeSeed,
  RestoreDefaults,
  CopySetup,
  Create
};

struct NativeNewGameIntent {
  NativeNewGameIntentKind kind{NativeNewGameIntentKind::None};
  bool captured{};
  std::string species_id, seed_text;
  int system_count{}, pre_warp_civilization_count{}, ancient_civilization_count{};
  stellar::core::StellarPopulationOptions stellar_population;
  stellar::core::DeveloperResearchOptions developer_research;
  bool developer_full_coverage{};
  stellar::core::PopulationSelection requested_population{stellar::core::PopulationSelection::Random};
  bool developer_full_exploration{};
};

class NativeNewGameWorkspace final {
public:
  using TextMeasurer = std::function<stellar::native_map::TextExtent(
      const stellar::native_map::Text &)>;
  using PortraitProvider = std::function<std::shared_ptr<
      const stellar::native_map::RgbaImage>(std::string_view asset_path)>;

  void set_hover_callback(std::function<void()> callback){hover_feedback_.set_callback(std::move(callback));}
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  void set_view(stellar::native_setup::NativeNewCampaignSetupView);
  void clear() noexcept;
  void set_assessment_message(std::string message, bool accepted);
  void randomize_seed();
  void begin_sandbox();
  [[nodiscard]] SandboxPage page()const noexcept{return page_;}
  [[nodiscard]] std::optional<stellar::core::GalaxyMorphology> selected_morphology()const noexcept{return morphology_selected_?std::optional{population_.morphology}:std::nullopt;}
  [[nodiscard]] stellar::core::PopulationSelection requested_population()const noexcept{return requested_population_;}
  [[nodiscard]] std::optional<stellar::core::GalaxyGenerationConfig> generation_configuration()const;


  [[nodiscard]] const std::optional<
      stellar::native_setup::NativeNewCampaignSetupView> &
  view() const noexcept {
    return view_;
  }
  [[nodiscard]] const std::string &selected_species_id() const noexcept {
    return selected_species_id_;
  }
  [[nodiscard]] int selected_system_count() const noexcept {
    return selected_system_count_;
  }
  [[nodiscard]] int selected_pre_warp_civilization_count() const noexcept {
    return selected_pre_warp_civilization_count_;
  }
  [[nodiscard]] int selected_ancient_civilization_count() const noexcept {
    return selected_ancient_civilization_count_;
  }
  [[nodiscard]] const std::string &seed_text() const noexcept {
    return seed_text_;
  }
  [[nodiscard]] bool seed_focused() const noexcept { return seed_focused_; }
  [[nodiscard]] float detail_scroll() const noexcept { return detail_scroll_; }

  [[nodiscard]] NativeNewGameMeasuredLayout measure_layout(
      int width, int height, const TextMeasurer &) const;
  [[nodiscard]] NativeNewGameIntent handle(
      const stellar::native_map::InputEvent &, int width, int height,
      const TextMeasurer &);
  void render(stellar::native_map::DrawList &, int width, int height,
              const TextMeasurer &,
              const PortraitProvider *portrait_provider = nullptr,
              std::shared_ptr<const stellar::native_map::RgbaImage> background =
                  {}) const;

private:
  NativeNewGameIntent handle_galaxy_page(const stellar::native_map::InputEvent&,int,int);
  void render_galaxy_page(stellar::native_map::DrawList&,int,int,const PortraitProvider*,std::shared_ptr<const stellar::native_map::RgbaImage>)const;
  SandboxPage page_{SandboxPage::Configuration};
  bool morphology_selected_{};
  stellar::core::PopulationSelection requested_population_{stellar::core::PopulationSelection::Random};
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  stellar::native_ui::Dropdown dropdown_;
  [[nodiscard]] std::optional<std::size_t> species_hit(
      stellar::native_map::Point,
      const NativeNewGameMeasuredLayout &) const noexcept;
  [[nodiscard]] std::optional<std::size_t> size_hit(
      stellar::native_map::Point, const NativeNewGameLayout &) const noexcept;
  void reconcile();
  void restore_defaults();
  void reset_interaction() noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string trf(std::string_view key,
                                std::initializer_list<std::string> args,
                                std::string_view fallback) const;

  std::optional<stellar::native_setup::NativeNewCampaignSetupView> view_;
  std::string selected_species_id_, seed_text_, message_;
  stellar::core::StellarPopulationOptions population_;
  stellar::core::DeveloperResearchOptions developer_research_;
  bool developer_coverage_{};
  bool developer_exploration_{};
  int selected_system_count_{}, selected_pre_warp_civilization_count_{},
      selected_ancient_civilization_count_{};
  float species_scroll_{}, detail_scroll_{};
  bool seed_focused_{}, seed_replace_pending_{}, assessment_accepted_{}, pressed_{};
  stellar::native_map::Point pointer_{};
  const stellar::engine::LocalizationTable *locale_{};
};

} // namespace stellar::native_setup_ui
