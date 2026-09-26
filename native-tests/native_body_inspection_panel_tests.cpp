#include "native_body_inspection_panel.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {
using namespace stellar::native_map;
using namespace stellar::native_system_ui;

constexpr UiRect inspector{935.f, 180.f, 326.f, 522.f};
constexpr float footer_top = 616.f;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Body inspection panel check failed at line " +
                             std::to_string(line) + ": " + std::string(expression));
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

bool contains(const DrawList &draw, std::string_view value) {
  return std::ranges::any_of(draw.overlay, [&](const auto &item) {
    const auto *text = std::get_if<Text>(&item);
    return text && text->value == value;
  });
}

const Text &text(const DrawList &draw, std::string_view value) {
  for (const auto &item : draw.overlay)
    if (const auto *candidate = std::get_if<Text>(&item);
        candidate && candidate->value == value)
      return *candidate;
  throw std::runtime_error("Rendered text missing: " + std::string(value));
}

bool inside(UiRect outer, UiRect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

BodyInspection inspection(int id = 1, std::string name = "Erebus") {
  BodyInspection value;
  value.body_id = id;
  value.name = std::move(name);
  value.survey_status = "SURVEY COMPLETE";
  value.confirmed = true;
  value.sections = {
      {"Physical", {{"Type", "Planet"}, {"Radius", "6,371 km"},
                    {"Mass", "5.972 × 10²⁴ kg"}, {"Gravity", "9.81 m/s²"},
                    {"Eccentricity", "0.0167"}, {"Inclination", "0°"}}},
      {"Environment", {{"Temperature", "288 K"}, {"Pressure", "101.3 kPa"},
                         {"Atmosphere", "Oxygen / nitrogen"}}},
      {"Satellites & signals", {{"Known moons", "2"},
                                {"Resources", "Signal detected"},
                                {"Activity", "Signal detected"},
                                {"Anomaly", "Signal detected"}}}};
  return value;
}

int measured_calls{};
TextExtent measured(const Text &value) {
  ++measured_calls;
  const auto width = std::max(1.f, value.wrap_width);
  const auto glyphs = std::max<std::size_t>(1, value.value.size());
  const auto natural = static_cast<float>(glyphs * std::max(5, value.font_pixel_size) * .55);
  const auto lines = std::max(1, static_cast<int>(std::ceil(natural / width)));
  return {static_cast<int>(std::min(natural, width)), lines * (value.font_pixel_size + 5)};
}

void assert_clipped(const DrawList &draw) {
  for (const auto &item : draw.overlay) {
    const auto *candidate = std::get_if<Text>(&item);
    if (!candidate)
      continue;
    REQUIRE(candidate->clip.has_value());
    REQUIRE(inside(inspector, *candidate->clip));
    if (candidate->clip->width < inspector.width) {
      REQUIRE(candidate->clip->y >= inspector.y);
      REQUIRE(candidate->clip->y + candidate->clip->height <= footer_top - 8.f);
    }
  }
}
} // namespace

int main() {
  try {
    BodyInspectionPanel panel;
    panel.set_text_measurer(measured);
    panel.set_inspection(inspection());
    DrawList initial;
    panel.render(initial, inspector, footer_top);
    REQUIRE(panel.visible());
    REQUIRE(contains(initial, "SYSTEM INSPECTOR") && contains(initial, "Erebus"));
    REQUIRE(text(initial, "SYSTEM INSPECTOR").at.y == inspector.y + 14.f);
    REQUIRE(contains(initial, "Physical"));
    assert_clipped(initial);

    measured_calls = 0;
    DrawList cached;
    panel.render(cached, inspector, footer_top);
    REQUIRE(measured_calls == 0);
    auto renamed = inspection(1, "Renamed world");
    panel.set_inspection(renamed);
    DrawList renamed_draw;
    panel.render(renamed_draw, inspector, footer_top);
    REQUIRE(measured_calls > 0 && contains(renamed_draw, "Renamed world"));
    measured_calls = 0;
    panel.set_text_measurer([](const Text &value) {
      ++measured_calls;
      return TextExtent{static_cast<int>(value.wrap_width), value.font_pixel_size + 7};
    });
    DrawList remeasured;
    panel.render(remeasured, inspector, footer_top);
    REQUIRE(measured_calls > 0);
    measured_calls = 0;
    DrawList resized;
    const UiRect resized_panel{935.f, 180.f, 300.f, 500.f};
    panel.render(resized, resized_panel, footer_top);
    REQUIRE(measured_calls > 0);

    panel.set_text_measurer(measured);
    panel.set_inspection(inspection());
    panel.scroll(-10000.f, inspector, footer_top);
    REQUIRE(panel.scroll_offset() > 0.f);
    DrawList end;
    panel.render(end, inspector, footer_top);
    REQUIRE(contains(end, "Signal detected"));
    REQUIRE(text(end, "SYSTEM INSPECTOR").at.y == inspector.y + 14.f);
    assert_clipped(end);
    panel.scroll(10000.f, inspector, footer_top);
    REQUIRE(panel.scroll_offset() == 0.f);
    panel.scroll(-10000.f, inspector, footer_top);
    const auto end_offset = panel.scroll_offset();
    panel.scroll(-10000.f, inspector, footer_top);
    REQUIRE(panel.scroll_offset() == end_offset);
    panel.set_inspection(inspection(2, "New target"));
    REQUIRE(panel.scroll_offset() == 0.f);

    panel.set_inspection(inspection(1, "Erebus"));
    DrawList complete;
    panel.render(complete, inspector, footer_top);
    REQUIRE(contains(complete, "6,371 km"));
    auto redacted = inspection(1, "Erebus");
    redacted.confirmed = false;
    redacted.survey_status = "DETAILED SURVEY NEEDED";
    for (auto &section : redacted.sections)
      for (auto &fact : section.facts)
        if (fact.label == "Radius" || fact.label == "Mass" ||
            fact.label == "Temperature")
          fact.value = "Unconfirmed";
    panel.set_inspection(redacted);
    DrawList downgraded;
    panel.render(downgraded, inspector, footer_top);
    REQUIRE(contains(downgraded, "Unconfirmed"));
    REQUIRE(!contains(downgraded, "6,371 km"));
    REQUIRE(!contains(downgraded, "5.972 × 10²⁴ kg"));
    REQUIRE(!contains(downgraded, "288 K"));

    std::string long_name;
    for (int index = 0; index < 100; ++index)
      long_name += "\xE2\x98\x85";
    auto long_value = inspection(3, long_name);
    long_value.sections.front().facts.front().value =
        "A deliberately measured multiline fact that must remain inside the scrolling body.";
    panel.set_inspection(std::move(long_value));
    DrawList long_draw;
    panel.render(long_draw, inspector, footer_top);
    REQUIRE(contains(long_draw, "SYSTEM INSPECTOR"));
    REQUIRE(text(long_draw, "SYSTEM INSPECTOR").at.y == inspector.y + 14.f);
    REQUIRE(text(long_draw, "SURVEY COMPLETE").at.y < footer_top);
    assert_clipped(long_draw);

    // 1080p uses the native workspace's wider/taller inspector footprint.
    const UiRect inspector_1080{1522.f, 180.f, 380.f, 882.f};
    constexpr float footer_1080 = 974.f;
    BodyInspectionPanel high_resolution;
    high_resolution.set_text_measurer(measured);
    high_resolution.set_inspection(inspection(4, "Συνέχεια"));
    DrawList high_draw;
    high_resolution.render(high_draw, inspector_1080, footer_1080);
    REQUIRE(contains(high_draw, "SYSTEM INSPECTOR") && contains(high_draw, "Συνέχεια") &&
            contains(high_draw, "Satellites & signals"));
    for (const auto &item : high_draw.overlay)
      if (const auto *candidate = std::get_if<Text>(&item); candidate) {
        REQUIRE(candidate->clip && inside(inspector_1080, *candidate->clip));
        if (candidate->clip->width < inspector_1080.width)
          REQUIRE(candidate->clip->y + candidate->clip->height <= footer_1080 - 8.f);
      }

    // Minimum-height panels collapse the name banner so the scrollable fact
    // list keeps a usable viewport instead of clipping to zero height.
    const UiRect inspector_tiny{60.f, 128.f, 220.f, 167.f};
    constexpr float footer_tiny = 232.f;
    BodyInspectionPanel tiny;
    tiny.set_text_measurer(measured);
    tiny.set_inspection(inspection(5, "Erebus"));
    DrawList tiny_draw;
    tiny.render(tiny_draw, inspector_tiny, footer_tiny);
    REQUIRE(contains(tiny_draw, "SYSTEM INSPECTOR") &&
            contains(tiny_draw, "SURVEY COMPLETE"));
    REQUIRE(!contains(tiny_draw, "Erebus"));
    REQUIRE(tiny.scroll_offset() == 0.f);
    std::set<std::string> reachable;
    for (const auto &item : tiny_draw.overlay)
      if (const auto *candidate = std::get_if<Text>(&item); candidate)
        reachable.insert(candidate->value);
    float last_scroll = -1.f;
    for (int step = 0; step < 256 && tiny.scroll_offset() != last_scroll; ++step) {
      last_scroll = tiny.scroll_offset();
      tiny.scroll(-0.5f, inspector_tiny, footer_tiny);
      DrawList frame;
      tiny.render(frame, inspector_tiny, footer_tiny);
      for (const auto &item : frame.overlay)
        if (const auto *candidate = std::get_if<Text>(&item); candidate)
          reachable.insert(candidate->value);
    }
    for (const char *required :
         {"Physical", "6,371 km", "9.81 m/s²", "Environment",
          "Oxygen / nitrogen", "Satellites & signals", "Known moons"})
      if (reachable.count(required) != 1) {
        std::cerr << "unreachable: " << required << " scroll=" << tiny.scroll_offset()
                  << " reachable:";
        for (const auto &entry : reachable) std::cerr << " [" << entry << "]";
        std::cerr << '\n';
        REQUIRE(false);
      }
    REQUIRE(tiny.scroll_offset() > 0.f);
    for (const auto &item : tiny_draw.overlay)
      if (const auto *candidate = std::get_if<Text>(&item); candidate) {
        REQUIRE(candidate->clip && inside(inspector_tiny, *candidate->clip));
        if (candidate->clip->width < inspector_tiny.width)
          REQUIRE(candidate->clip->y + candidate->clip->height <= footer_tiny - 8.f);
      }

    panel.clear();
    DrawList cleared;
    panel.render(cleared, inspector, footer_top);
    REQUIRE(!panel.visible() && cleared.overlay.empty());
    std::cout << "native body inspection panel tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
