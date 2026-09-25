#include <stellar/engine/accessibility.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace stellar::engine {
namespace {

float clamp_scale(float value) noexcept {
  if (!std::isfinite(value))
    return 1.0f;
  return std::clamp(value, 0.75f, 2.0f);
}

ColorBlindMode parse_mode(const std::string &name) {
  if (name == "protanopia")
    return ColorBlindMode::Protanopia;
  if (name == "deuteranopia")
    return ColorBlindMode::Deuteranopia;
  if (name == "tritanopia")
    return ColorBlindMode::Tritanopia;
  return ColorBlindMode::None;
}

const char *mode_name(ColorBlindMode mode) {
  switch (mode) {
  case ColorBlindMode::Protanopia: return "protanopia";
  case ColorBlindMode::Deuteranopia: return "deuteranopia";
  case ColorBlindMode::Tritanopia: return "tritanopia";
  default: return "none";
  }
}

} // namespace

void AccessibilitySettings::sanitize() {
  ui_scale = clamp_scale(ui_scale);
  text_scale = clamp_scale(text_scale);
  subtitle_scale = clamp_scale(subtitle_scale);
}

std::string AccessibilitySettings::to_json() const {
  nlohmann::json doc;
  doc["ui_scale"] = ui_scale;
  doc["text_scale"] = text_scale;
  doc["high_contrast"] = high_contrast;
  doc["color_blind"] = mode_name(color_blind);
  doc["reduce_motion"] = reduce_motion;
  doc["reduce_flashing"] = reduce_flashing;
  doc["subtitles_enabled"] = subtitles_enabled;
  doc["subtitle_scale"] = subtitle_scale;
  return doc.dump();
}

AccessibilitySettings
AccessibilitySettings::from_json(std::string_view document) {
  AccessibilitySettings result;
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(document);
  } catch (...) {
    return result;
  }
  if (!doc.is_object())
    return result;
  result.ui_scale = doc.value("ui_scale", result.ui_scale);
  result.text_scale = doc.value("text_scale", result.text_scale);
  result.high_contrast = doc.value("high_contrast", result.high_contrast);
  result.color_blind = parse_mode(doc.value("color_blind", std::string{}));
  result.reduce_motion = doc.value("reduce_motion", result.reduce_motion);
  result.reduce_flashing =
      doc.value("reduce_flashing", result.reduce_flashing);
  result.subtitles_enabled =
      doc.value("subtitles_enabled", result.subtitles_enabled);
  result.subtitle_scale =
      doc.value("subtitle_scale", result.subtitle_scale);
  result.sanitize();
  return result;
}

void AccessibilityAnnouncer::announce(std::string text,
                                      AnnouncementPriority priority) {
  announce(std::move(text), priority, AnnouncementKind::Status, std::nullopt,
           std::nullopt, AnnouncementControl::Custom);
}

void AccessibilityAnnouncer::announce(std::string text,
                                      AnnouncementPriority priority,
                                      AnnouncementKind kind,
                                      std::optional<AnnouncementBounds> bounds,
                                      std::optional<AnnouncementRange> range,
                                      AnnouncementControl control,
                                      std::optional<bool> checked,
                                      std::optional<AnnouncementValue> value) {
  // Empty status text is dropped, but an empty Focus item is meaningful:
  // it marks the ring releasing, so platform bridges can retire the stale
  // focused fragment instead of leaving the last label claiming focus.
  if (text.empty() && kind != AnnouncementKind::Focus)
    return;
  if (!pending_.empty() && pending_.back().text == text &&
      pending_.back().priority == priority && pending_.back().kind == kind &&
      pending_.back().range == range && pending_.back().checked == checked &&
      pending_.back().value == value)
    return;
  if (priority == AnnouncementPriority::Assertive) {
    std::erase_if(pending_, [](const AccessibilityAnnouncement &item) {
      return item.priority == AnnouncementPriority::Polite;
    });
  }
  while (pending_.size() >= capacity_) {
    const auto polite = std::find_if(
        pending_.begin(), pending_.end(), [](const AccessibilityAnnouncement &i) {
          return i.priority == AnnouncementPriority::Polite;
        });
    pending_.erase(polite != pending_.end() ? polite : pending_.begin());
  }
  pending_.push_back(AccessibilityAnnouncement{
      std::move(text), priority, kind, std::move(bounds), std::move(range),
      control, checked, std::move(value), sequence_++});
}

std::optional<AccessibilityAnnouncement> AccessibilityAnnouncer::take() {
  if (pending_.empty())
    return std::nullopt;
  auto item = std::move(pending_.front());
  pending_.pop_front();
  return item;
}

const AccessibilityAnnouncement *AccessibilityAnnouncer::latest() const noexcept {
  return pending_.empty() ? nullptr : &pending_.back();
}

} // namespace stellar::engine
