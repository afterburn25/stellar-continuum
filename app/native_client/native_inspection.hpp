#pragma once

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace stellar::native_inspection {
struct InspectionFact { std::string label, value; bool positive{}; [[nodiscard]] bool operator==(const InspectionFact&) const = default; };
struct OwnSettlementInspection {
  int colony_id{};
  std::string name, body;
  double population_millions{}, infrastructure{}, stability{};
  [[nodiscard]] bool operator==(const OwnSettlementInspection&) const = default;
};
struct SystemInspection {
  int selected_system_id{-1}, observer_id{};
  std::string name{"SELECT A STAR"}, survey_status{"No target"}, guidance;
  double survey_progress{};
  bool developer_inspection{};
  std::vector<InspectionFact> facts;
  std::vector<OwnSettlementInspection> own_settlements;
  std::string foreign_settlement_intelligence{"Foreign settlement intelligence unavailable"};
  [[nodiscard]] bool operator==(const SystemInspection&) const = default;
};
[[nodiscard]] SystemInspection build_system_inspection(
    const stellar::core::FreshCampaignState&, int selected_system_id,
    const stellar::engine::LocalizationTable *locale = nullptr);

struct InspectionHandleResult { bool captured{}, closed{}; };
class SystemInspectionCard final {
public:
  void set_text_measurer(std::function<stellar::native_map::TextExtent(
      const stellar::native_map::Text&)>);
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  void set_inspection(SystemInspection);
  void clear() noexcept;
  [[nodiscard]] static stellar::native_map::UiRect close_bounds(stellar::native_map::UiRect) noexcept;
  [[nodiscard]] static stellar::native_map::UiRect body_bounds(stellar::native_map::UiRect) noexcept;
  [[nodiscard]] float scroll_offset() const noexcept { return scroll_.scroll_offset; }
  [[nodiscard]] bool visible() const noexcept { return inspection_.has_value(); }
  [[nodiscard]] int focus() const noexcept { return focus_; }
  [[nodiscard]] InspectionHandleResult handle(const stellar::native_map::InputEvent&,
                                              stellar::native_map::UiRect);
  void render(stellar::native_map::DrawList&, stellar::native_map::UiRect) const;
private:
  std::optional<SystemInspection> inspection_;
  mutable stellar::engine::ScrollView scroll_{};
  mutable std::optional<stellar::native_map::UiRect> last_bounds_;
  std::function<stellar::native_map::TextExtent(
      const stellar::native_map::Text&)> text_measurer_;
  const stellar::engine::LocalizationTable *locale_{};
  bool pointer_owned_{};
  int focus_{-1};
};
} // namespace stellar::native_inspection
