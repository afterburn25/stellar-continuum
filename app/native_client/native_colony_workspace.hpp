#pragma once

#include "native_colony_controller.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>

namespace stellar::native_colony_ui {

struct ColonyWorkspaceLayout {
  float scale{};
  int title_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  stellar::native_map::UiRect surface, title, close, open_surface;
  stellar::native_map::UiRect details, summary, sustenance, operations, sites,
      site_rows;

  [[nodiscard]] static ColonyWorkspaceLayout for_viewport(int width,
                                                           int height) noexcept;
};

enum class ColonyWorkspaceCommandKind { None, Close, OpenSurface };

struct ColonyWorkspaceCommand {
  ColonyWorkspaceCommandKind kind{ColonyWorkspaceCommandKind::None};
  bool captured{};
};

class NativeColonyWorkspace final {
public:
  void open(stellar::native_colony::NativeColonyView);
  void set_view(stellar::native_colony::NativeColonyView);
  void close() noexcept;
  void discard_campaign() noexcept;

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
  std::optional<stellar::native_colony::NativeColonyView> view_;
  stellar::native_map::Point pointer_{};
  float site_scroll_{};
  float detail_scroll_{};
};

} // namespace stellar::native_colony_ui
