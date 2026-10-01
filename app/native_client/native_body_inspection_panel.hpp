#pragma once
#include "native_body_inspection.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <functional>

namespace stellar::native_system_ui {
class BodyInspectionPanel {
public:
  void set_inspection(std::optional<BodyInspection>);
  void set_text_measurer(std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)>);
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept { locale_ = table; }
  void clear();
  bool visible() const noexcept { return value_.has_value(); }
  float scroll_offset() const noexcept { return scroll_.scroll_offset; }
  void scroll(float wheel,stellar::native_map::UiRect panel,float footer_top);
  void render(stellar::native_map::DrawList&,stellar::native_map::UiRect panel,float footer_top) const;
private:
  struct Item {float x{},y{},width{},height{};std::string text;bool heading{};};
  void layout(stellar::native_map::UiRect,float) const;
  const stellar::engine::LocalizationTable *locale_{};
  std::optional<BodyInspection> value_;
  std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure_;
  mutable std::vector<Item> items_;
  mutable stellar::native_map::UiRect panel_{},body_{};
  mutable float footer_top_{},name_height_{},status_inset_{50.f};
  mutable bool compact_header_{};
  mutable stellar::engine::ScrollView scroll_{};
  mutable bool valid_{};
};
}
