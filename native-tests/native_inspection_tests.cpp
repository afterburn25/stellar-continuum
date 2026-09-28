#include "native_inspection.hpp"

#include <stellar/core/fleet_reach.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

using namespace stellar::core;
using namespace stellar::native_inspection;
using namespace stellar::native_map;

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

FreshCampaignState world() {
  FreshCampaignState value;
  value.player_civilization_id = 1;
  value.systems = {{1, "Sol", {0, 0, 0.0}},
                   {2, "Known Star", {3, 4, 12.0}},
                   {3, "Other", {7, 0, 0.0}}};
  value.systems[1].primary = StellarClass::MRedDwarf;
  value.systems[1].archetype = StarArchetype::ResourceRich;
  value.systems[1].has_anomaly = true;
  value.civilizations = {{1, "Player", 1, {}, {}, true},
                         {2, "Foreign", 2},
                         {3, "Other", 2}};
  value.bodies = {{10, 2, {}, 1, "Aster"},
                  {11, 2, {}, 2, "Boreal"},
                  {12, 3, {}, 1, "Wrong system"}};
  return value;
}

Colony colony(int id, int owner, std::string name,
              std::optional<int> body = std::nullopt) {
  Colony value;
  value.id = id;
  value.civilization_id = owner;
  value.system_id = 2;
  value.planetary_body_id = body;
  value.name = std::move(name);
  value.population_millions = 44.0;
  value.infrastructure = 7.0;
  value.stability = .2;
  return value;
}

void set_survey(FreshCampaignState& value, SystemSurveyLevel level) {
  if (level == SystemSurveyLevel::detected)
    (void)value.knowledge.reveal_system(1, 2);
  else if (level == SystemSurveyLevel::partially_surveyed)
    (void)value.knowledge.record_reconnaissance(1, 2, .42);
  else if (level == SystemSurveyLevel::fully_surveyed)
    (void)value.knowledge.mark_system_fully_surveyed(1, 2);
}

const InspectionFact& fact(const SystemInspection& inspection,
                           std::string_view label) {
  const auto found = std::ranges::find(inspection.facts, label,
                                       &InspectionFact::label);
  require(found != inspection.facts.end(), "inspection fact missing");
  return *found;
}

void foreign_state_never_leaks() {
  constexpr std::array levels{SystemSurveyLevel::unknown,
                              SystemSurveyLevel::detected,
                              SystemSurveyLevel::partially_surveyed,
                              SystemSurveyLevel::fully_surveyed};
  for (const auto level : levels) {
    auto first = world();
    first.colonies = {colony(20, 2, "Secret Hold", 10),
                      colony(21, 3, "Other Secret", 11)};
    (void)first.knowledge.reveal_civilization(1, 2);
    (void)first.knowledge.reveal_civilization(1, 3);
    set_survey(first, level);

    auto mutated = first;
    mutated.colonies[0].name = "Mutated colony";
    mutated.colonies[0].population_millions = 999.0;
    mutated.colonies[0].planetary_body_id = 11;
    mutated.bodies[0].name = "Mutated foreign body";
    mutated.colonies[0].civilization_id = 3;
    std::ranges::reverse(mutated.colonies);
    mutated.colonies.push_back(colony(22, 2, "Extra secret", 10));
    auto removed = first;
    removed.colonies.clear();
    require(build_system_inspection(first, 2) ==
                build_system_inspection(mutated, 2),
            "foreign colony mutation leaked into the DTO");
    require(build_system_inspection(first, 2) ==
                build_system_inspection(removed, 2),
            "foreign colony removal leaked into the DTO");
  }
}

void survey_redaction_and_public_distance() {
  auto first = world();
  auto second = first;
  second.systems[1].name = "Hidden renamed star";
  second.systems[1].primary = StellarClass::BlackHole;
  second.systems[1].archetype = StarArchetype::Legendary;
  second.systems[1].has_anomaly = false;
  second.systems[1].has_habitable_world = true;
  require(build_system_inspection(first, 2) ==
              build_system_inspection(second, 2),
          "unknown star details leaked into the DTO");

  const auto unknown = build_system_inspection(first, 2);
  require(unknown.name == "UNKNOWN" && unknown.facts.size() == 1,
          "unknown system was not redacted");
  require(fact(unknown, "DISTANCE FROM HOMEWORLD").value ==
              format_interstellar_metric_primary(13.0),
          "home distance did not use the authoritative 3D coordinate rule");

  set_survey(first, SystemSurveyLevel::detected);
  const auto detected = build_system_inspection(first, 2);
  require(detected.name == "Known Star" && detected.facts.size() == 1,
          "detected system did not expose only its observed name and distance");
  set_survey(second, SystemSurveyLevel::partially_surveyed);
  const auto partial = build_system_inspection(second, 2);
  require(partial.name == "Hidden renamed star" && partial.facts.size() == 1,
          "partial survey name or redaction was incorrect");
}

void all_enum_labels() {
  constexpr std::array classes{
      StellarClass::MRedDwarf, StellarClass::KOrangeDwarf,
      StellarClass::GYellowDwarf, StellarClass::FYellowWhiteDwarf,
      StellarClass::AWhiteStar, StellarClass::HotBlueStar,
      StellarClass::Giant, StellarClass::WhiteDwarf,
      StellarClass::NeutronStar, StellarClass::BlackHole,
      StellarClass::Protostar, StellarClass::Pulsar};
  constexpr std::array archetypes{
      StarArchetype::Standard, StarArchetype::ResourceRich,
      StarArchetype::HabitableRich, StarArchetype::BarrenFrontier,
      StarArchetype::Nebula, StarArchetype::NeutronPulsar,
      StarArchetype::BlackHole, StarArchetype::AncientRuin,
      StarArchetype::Dangerous, StarArchetype::Legendary};
  auto value = world();
  set_survey(value, SystemSurveyLevel::fully_surveyed);
  for (const auto item : classes) {
    value.systems[1].primary = item;
    const auto label = fact(build_system_inspection(value, 2), "PRIMARY STAR").value;
    require(!label.empty() && label != "Legacy classification",
            "stellar class label is missing");
  }
  for (const auto item : archetypes) {
    value.systems[1].archetype = item;
    const auto label = fact(build_system_inspection(value, 2), "SYSTEM TRAITS").value;
    require(!label.empty() && label != "Unknown",
            "star archetype label is missing");
  }
}

void stellar_physics_requires_completed_observation() {
  for(const auto level:{SystemSurveyLevel::unknown,SystemSurveyLevel::detected,SystemSurveyLevel::partially_surveyed}) {
    auto ordinary=world();set_survey(ordinary,level);
    ordinary.systems[1].stellar_object=generate_stellar_physics(41,StellarObjectType::GYellowStar);
    auto secret=ordinary;
    secret.systems[1].stellar_object=generate_stellar_physics(91,StellarObjectType::JetBlackHole);
    require(build_system_inspection(ordinary,2)==build_system_inspection(secret,2),
            "undiscovered stellar physics, rarity or jets leaked into inspection");
  }
  auto observed=world();set_survey(observed,SystemSurveyLevel::fully_surveyed);
  observed.systems[1].stellar_object=generate_stellar_physics(91,StellarObjectType::JetBlackHole);
  const auto inspection=build_system_inspection(observed,2);
  require(!fact(inspection,"STELLAR RADIUS").value.empty()&&
          fact(inspection,"STELLAR HAZARD").value=="Directional high-energy jets",
          "completed observation did not reveal physical measurements and hazards");
}

void own_colonies_are_complete_sorted_and_body_safe() {
  auto value = world();
  set_survey(value, SystemSurveyLevel::fully_surveyed);
  auto later = colony(8, 1, "Alpha Base", 11);
  auto earlier = colony(7, 1, "Alpha Base", 10);
  auto invalid = colony(9, 1, "Beta Base", 12);
  value.colonies = {later, colony(4, 2, "Foreign", 10), invalid, earlier};
  const auto inspection = build_system_inspection(value, 2);
  require(inspection.own_settlements.size() == 3,
          "not all own colonies were listed");
  require(inspection.own_settlements[0].colony_id == 7 &&
              inspection.own_settlements[1].colony_id == 8 &&
              inspection.own_settlements[2].colony_id == 9,
          "own colonies were not sorted by name and ID");
  require(inspection.own_settlements[0].body == "Aster" &&
              inspection.own_settlements[2].body == "Unassigned body",
          "colony body did not validate membership in the selected system");
  require(inspection.own_settlements[0].population_millions == 44.0 &&
              inspection.own_settlements[0].infrastructure == 7.0 &&
              inspection.own_settlements[0].stability == .2,
          "stored colony fields were not projected directly");
}

void invalid_observer_and_missing_target_are_independent() {
  auto missing_observer = world();
  missing_observer.player_civilization_id = 99;
  const auto observer = build_system_inspection(missing_observer, 2);
  require(observer.name == "INTELLIGENCE UNAVAILABLE" &&
              observer.survey_status == "Observer unavailable",
          "invalid observer was accepted");
  auto missing_target = world();
  const auto target = build_system_inspection(missing_target, 99);
  require(target.name == "TARGET LOST" && target.observer_id == 1,
          "missing target was not reported independently");
  const auto none = build_system_inspection(missing_target, -1);
  require(none.name == "SELECT A STAR",
          "negative target did not retain the no-target state");
}

bool contained(UiRect outer, UiRect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

void require_body_text_clipped(const DrawList& draw, UiRect bounds) {
  const auto body = SystemInspectionCard::body_bounds(bounds);
  std::size_t body_labels{};
  for (const auto& command : draw.overlay) {
    const auto* label = std::get_if<Text>(&command);
    if (!label) continue;
    const bool pinned = label->value == "Known Star" ||
                        label->value == "Fully surveyed" ||
                        label->value == "100%" || label->value == "X";
    require(label->clip.has_value(), "card text was not clipped");
    if (pinned) continue;
    require(contained(body, *label->clip),
            "scrolled body text clip escaped the body viewport");
    ++body_labels;
  }
  require(body_labels > 0, "scroll position rendered no body text");
}

SystemInspection long_inspection() {
  auto value = world();
  set_survey(value, SystemSurveyLevel::fully_surveyed);
  for (int id = 0; id < 8; ++id)
    value.colonies.push_back(colony(100 + id, 1,
                                    "Settlement " + std::to_string(id), 10));
  return build_system_inspection(value, 2);
}

void card_layout_scroll_and_refresh() {
  SystemInspectionCard card;
  const UiRect short_bounds{80, 90, 360, 180};
  card.set_inspection(long_inspection());
  DrawList top_draw;
  card.render(top_draw, short_bounds);
  require_body_text_clipped(top_draw, short_bounds);
  require(card.handle({InputEventType::Wheel, {100, 150}, {}, -10},
                      short_bounds).captured,
          "middle-scroll wheel was not captured");
  DrawList middle_draw;
  card.render(middle_draw, short_bounds);
  require_body_text_clipped(middle_draw, short_bounds);
  require(card.handle({InputEventType::Wheel, {100, 150}, {}, -1000},
                      short_bounds).captured,
          "wheel inside card was not captured");
  DrawList first_draw;
  card.render(first_draw, short_bounds);
  require_body_text_clipped(first_draw, short_bounds);
  const float end = card.scroll_offset();
  require(end > 0.f, "long card did not scroll");
  (void)card.handle({InputEventType::Wheel, {100, 150}, {}, -1000},
                    short_bounds);
  require(card.scroll_offset() == end, "scroll did not clamp at exact end");

  SystemInspectionCard measured;
  measured.set_text_measurer([](const Text& text) {
    const int height = text.value.size() > 12 ? 80 : 20;
    return TextExtent{static_cast<int>(text.wrap_width), height};
  });
  measured.set_inspection(long_inspection());
  (void)measured.handle({InputEventType::Wheel,{100,150},{},-1000},
                        short_bounds);
  DrawList measured_draw;
  measured.render(measured_draw,short_bounds);
  const float measured_end=measured.scroll_offset();
  require(measured_end>end,
          "renderer text measurement did not increase multiline scroll height");
  (void)measured.handle({InputEventType::Wheel,{100,150},{},-1000},
                        short_bounds);
  require(measured.scroll_offset()==measured_end,
          "measured layout did not clamp at its deterministic end");
  require_body_text_clipped(measured_draw,short_bounds);

  auto shorter = long_inspection();
  shorter.own_settlements.resize(1);
  card.set_inspection(shorter);
  require(card.scroll_offset() < end,
          "refresh with shorter content did not clamp scroll");
  DrawList resized_draw;
  card.render(resized_draw, {80, 90, 360, 2000});
  require(card.scroll_offset() == 0.f,
          "resize to a taller card did not clamp scroll");

  DrawList clipped;
  card.set_inspection(long_inspection());
  card.render(clipped, short_bounds);
  bool saw_title = false, saw_status = false, saw_progress = false;
  for (const auto& command : clipped.overlay) {
    if (const auto* label = std::get_if<Text>(&command)) {
      require(label->clip.has_value(), "card text was not clipped");
      if (label->value == "Known Star") saw_title = true;
      if (label->value == "Fully surveyed") saw_status = true;
    }
    if (const auto* rectangle = std::get_if<FilledRectangle>(&command)) {
      require(contained(short_bounds, rectangle->bounds),
              "card rectangle escaped its bounds");
      if (rectangle->bounds.height == 5.f) saw_progress = true;
    }
  }
  require(saw_title && saw_status && saw_progress,
          "card did not render clipped body, pinned header, and progress");
}

void card_pointer_ownership_close_and_keyboard() {
  SystemInspectionCard card;
  card.set_inspection(long_inspection());
  const UiRect bounds{80, 90, 360, 220};
  const Point inside{100, 150}, outside{20, 20};
  require(card.handle({InputEventType::LeftPressed, inside}, bounds).captured &&
              !card.handle({InputEventType::KeyPressed, inside}, bounds).captured &&
              card.handle({InputEventType::PointerMove, outside}, bounds).captured &&
              card.handle({InputEventType::LeftReleased, outside}, bounds).captured,
          "left pointer sequence was not owned across card bounds");
  require(!card.handle({InputEventType::PointerMove, outside}, bounds).captured,
          "left pointer ownership persisted after release");
  require(card.handle({InputEventType::RightPressed, inside}, bounds).captured &&
              card.handle({InputEventType::PointerMove, outside}, bounds).captured &&
              card.handle({InputEventType::PointerCancelled, outside}, bounds).captured,
          "right pointer sequence or cancellation was not owned");
  require(!card.handle({InputEventType::RightReleased, outside}, bounds).captured,
          "pointer ownership persisted after cancellation");
  require(card.handle({InputEventType::LeftPressed, inside}, bounds).captured,
          "target-switch setup press was not captured");
  auto switched = long_inspection();
  switched.selected_system_id = 3;
  card.set_inspection(std::move(switched));
  require(!card.handle({InputEventType::PointerMove, outside}, bounds).captured,
          "target switch retained pointer ownership");
  require(!card.handle({InputEventType::EscapePressed, inside}, bounds).captured &&
              !card.handle({InputEventType::KeyPressed, inside}, bounds).captured,
          "inspection swallowed keyboard input");

  const auto close = SystemInspectionCard::close_bounds(bounds);
  require(close.width == 24.f &&
              card.handle({InputEventType::LeftPressed,
                           {close.x + 2.f, close.y + 2.f}}, bounds).closed &&
              !card.visible(),
          "scaled close button did not dismiss the card");
}
} // namespace

int main() try {
  foreign_state_never_leaks();
  survey_redaction_and_public_distance();
  all_enum_labels();
  stellar_physics_requires_completed_observation();
  own_colonies_are_complete_sorted_and_body_safe();
  invalid_observer_and_missing_target_are_independent();
  card_layout_scroll_and_refresh();
  card_pointer_ownership_close_and_keyboard();
  std::cout << "native inspection tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
