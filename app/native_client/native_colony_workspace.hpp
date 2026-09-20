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
  stellar::native_map::UiRect surface, freight_review,
      freight_text, freight_confirm, freight_cancel;

  [[nodiscard]] static ColonyWorkspaceLayout for_viewport(int width,
                                                           int height) noexcept;
};

enum class ColonyWorkspaceCommandKind { None, Close, ReviewFreight, ConfirmFreight, CancelFreight, Planetary };

struct ColonyWorkspaceCommand {
  ColonyWorkspaceCommandKind kind{ColonyWorkspaceCommandKind::None};
  bool captured{};
  std::uint64_t quote_revision{};
  PlanetaryCommand planetary;
};

class NativeColonyWorkspace final {
public:
  void open(stellar::native_colony::NativeColonyView);
  bool planetary_modal()const{return visible_&&(planetary_.modal()||freight_preview_.has_value());}
  NativePlanetaryScreen& planetary(){return planetary_;}
  void set_view(stellar::native_colony::NativeColonyView);
  void close() noexcept;
  void discard_campaign() noexcept;
  void set_text_measurer(std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure) { planetary_.set_measurer(measure); measure_ = std::move(measure); }
  void set_freight_preview(stellar::native_colony::NativeOutpostFreightPreview);
  void cancel_freight() noexcept;
  void set_freight_notice(std::string notice) { planetary_.complete(std::move(notice)); }
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

  bool visible_{};
  NativePlanetaryScreen planetary_;
  std::optional<stellar::native_colony::NativeColonyView> view_;
  stellar::native_map::Point pointer_{};
  std::optional<stellar::native_colony::NativeOutpostFreightPreview> freight_preview_;
  std::string freight_text_;
  std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure_;
  mutable float freight_scroll_{};
  ColonyWorkspaceCommandKind freight_pressed_{ColonyWorkspaceCommandKind::None};
  [[nodiscard]] float freight_content_height(const ColonyWorkspaceLayout&) const;
};

} // namespace stellar::native_colony_ui
