#pragma once

#include "native_colony_controller.hpp"
#include "native_planetary_screen.hpp"
#include "native_outpost_freight_controller.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <functional>

namespace stellar::native_colony_ui {

struct ColonyWorkspaceLayout {
  float scale{};
  int title_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  stellar::native_map::UiRect surface, title, close, open_surface;
  stellar::native_map::UiRect details, summary, sustenance, operations, sites,
      site_rows;
  stellar::native_map::UiRect collect_freight, freight_notice, freight_review,
      freight_text, freight_confirm, freight_cancel;

  [[nodiscard]] static ColonyWorkspaceLayout for_viewport(int width,
                                                           int height, bool outpost = false) noexcept;
};

enum class ColonyWorkspaceCommandKind { None, Close, OpenSurface, ReviewFreight, ConfirmFreight, CancelFreight, Planetary };

struct ColonyWorkspaceCommand {
  ColonyWorkspaceCommandKind kind{ColonyWorkspaceCommandKind::None};
  bool captured{};
  std::uint64_t quote_revision{};
  PlanetaryCommand planetary;
};

class NativeColonyWorkspace final {
public:
  void open(stellar::native_colony::NativeColonyView);
  void use_planetary_screen(bool enabled=true){planetary_enabled_=enabled;}
  bool planetary_enabled()const{return planetary_enabled_;}
  bool planetary_modal()const{return visible_&&planetary_enabled_&&planetary_.modal();}
  NativePlanetaryScreen& planetary(){return planetary_;}
  void set_view(stellar::native_colony::NativeColonyView);
  void close() noexcept;
  void discard_campaign() noexcept;
  void set_text_measurer(std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure) { planetary_.set_measurer(measure); measure_ = std::move(measure); }
  void set_freight_preview(stellar::native_colony::NativeOutpostFreightPreview);
  void cancel_freight() noexcept;
  void set_freight_notice(std::string notice) { freight_notice_ = std::move(notice); }
  [[nodiscard]] const auto& freight_preview() const noexcept { return freight_preview_; }

  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const std::optional<stellar::native_colony::NativeColonyView> &
  view() const noexcept {
    return view_;
  }
  [[nodiscard]] ColonyWorkspaceCommand
  handle(const stellar::native_map::InputEvent &, int width, int height);
  void render(stellar::native_map::DrawList &, int width, int height) const;

private:
  void clamp_scroll(const ColonyWorkspaceLayout &) noexcept;

  bool visible_{};
  bool planetary_enabled_{}; // Host enables the player replacement. Legacy layout remains for migration regression probes.
  NativePlanetaryScreen planetary_;
  std::optional<stellar::native_colony::NativeColonyView> view_;
  stellar::native_map::Point pointer_{};
  float site_scroll_{};
  float detail_scroll_{};
  std::optional<stellar::native_colony::NativeOutpostFreightPreview> freight_preview_;
  std::string freight_notice_, freight_text_;
  std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure_;
  mutable float freight_scroll_{};
  ColonyWorkspaceCommandKind freight_pressed_{ColonyWorkspaceCommandKind::None};
  [[nodiscard]] float freight_content_height(const ColonyWorkspaceLayout&) const;
};

} // namespace stellar::native_colony_ui
