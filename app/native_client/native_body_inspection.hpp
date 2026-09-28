#pragma once

#include "native_system_view.hpp"

#include <stellar/engine/localization.hpp>

#include <optional>
#include <string>
#include <vector>

namespace stellar::native_system_ui {

struct BodyFact {
  std::string label;
  std::string value;
  bool operator==(const BodyFact &) const = default;
};

struct BodySection {
  std::string heading;
  std::vector<BodyFact> facts;
  bool operator==(const BodySection &) const = default;
};

struct BodyInspection {
  int body_id{};
  std::string name;
  std::string survey_status;
  bool confirmed{};
  std::vector<BodySection> sections;
  bool operator==(const BodyInspection &) const = default;
};

[[nodiscard]] std::optional<BodyInspection>
build_body_inspection(const native_system::NativeSystemSnapshot &, int body_id,
                      const stellar::engine::LocalizationTable *locale =
                          nullptr);

} // namespace stellar::native_system_ui
