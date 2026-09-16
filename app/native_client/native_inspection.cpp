#include "native_inspection.hpp"

#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/interstellar_distance.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string_view>
#include <tuple>

namespace stellar::native_inspection {
namespace {
using namespace stellar::core;
using namespace stellar::native_map;
std::string number(double value, int digits = 1) { std::ostringstream out; out << std::fixed << std::setprecision(digits) << value; return out.str(); }
void text(DrawList& out, UiRect box, std::string value, Color color, int font) {
  out.overlay.emplace_back(Text{{box.x, box.y}, std::move(value), color, font, box.width, box});
}
std::string star_label(const std::optional<StellarClass>& value) {
  if (!value) return "Unknown";
  switch (*value) { case StellarClass::MRedDwarf:return "M-type red dwarf"; case StellarClass::KOrangeDwarf:return "K-type orange dwarf"; case StellarClass::GYellowDwarf:return "G-type yellow dwarf"; case StellarClass::FYellowWhiteDwarf:return "F-type yellow-white dwarf"; case StellarClass::AWhiteStar:return "A-type white star"; case StellarClass::HotBlueStar:return "Hot blue B/O star"; case StellarClass::Giant:return "Red/orange giant"; case StellarClass::WhiteDwarf:return "White dwarf"; case StellarClass::NeutronStar:return "Neutron star / pulsar"; case StellarClass::Pulsar:return "Pulsar"; case StellarClass::BlackHole:return "Black hole"; case StellarClass::Protostar:return "Young star / protostar"; }
  return "Legacy classification";
}
std::string archetype_label(StarArchetype value) { switch(value) { case StarArchetype::Standard:return "Standard"; case StarArchetype::ResourceRich:return "Resource rich"; case StarArchetype::HabitableRich:return "Habitable rich"; case StarArchetype::BarrenFrontier:return "Barren frontier"; case StarArchetype::Nebula:return "Nebula"; case StarArchetype::NeutronPulsar:return "Neutron pulsar"; case StarArchetype::BlackHole:return "Black hole"; case StarArchetype::AncientRuin:return "Ancient ruins"; case StarArchetype::Dangerous:return "Dangerous"; case StarArchetype::Legendary:return "Legendary"; } return "Unknown"; }

float scale_for(UiRect bounds) { return std::clamp(bounds.width / 360.f, .72f, 2.4f); }

struct ContentLayout {
  struct Item {
    float x{}, y{}, width{}, height{};
    std::string value;
    Color color;
    int font{};
  };
  UiRect clip;
  float scale{}, height{};
  int body_font{}, small_font{};
  std::vector<Item> items;
};

float measured_height(std::string_view value,float width,int font,
    const std::function<TextExtent(const Text&)>& measurer,float fallback_line){
  if(measurer){
    const Text probe{{0.f,0.f},std::string(value),{},font,width};
    return static_cast<float>(std::max(1,measurer(probe).height));
  }
  const auto columns=std::max<std::size_t>(1,static_cast<std::size_t>(
      std::floor(width/std::max(1.f,font*.58f))));
  const auto lines=std::max<std::size_t>(1,(value.size()+columns-1)/columns);
  return static_cast<float>(lines)*fallback_line;
}

ContentLayout content_layout(const SystemInspection& value, UiRect bounds,
    const std::function<TextExtent(const Text&)>& measurer) {
  ContentLayout layout;
  layout.scale = scale_for(bounds);
  layout.clip = SystemInspectionCard::body_bounds(bounds);
  layout.body_font = std::max(11, static_cast<int>(14 * layout.scale));
  layout.small_font = std::max(9, static_cast<int>(11 * layout.scale));
  const float s = layout.scale;
  float y = 8.f * s;
  const auto add_block = [&](std::string text_value, float x, float width,
                             int font, float fallback_line, Color color) {
    const float height=measured_height(text_value,width,font,measurer,
                                       fallback_line);
    layout.items.push_back({x,y,width,height,std::move(text_value),color,font});
    y+=height;
  };
  add_block(value.guidance,0.f,layout.clip.width,layout.small_font,15.f*s,
            {163,193,214,255});
  y += 9.f * s;
  layout.items.push_back({0.f,y,layout.clip.width,14.f*s,"SURVEY FINDINGS",
                          {105,213,244,255},layout.small_font});
  y += 20.f * s;
  const float label_width=layout.clip.width*.44f;
  const float value_x=layout.clip.width*.46f;
  const float value_width=layout.clip.width*.54f;
  const auto add_row = [&](std::string label, std::string row_value,
                           Color value_color, int value_font,
                           float value_line_height) {
    const float row_y=y;
    const float label_height=measured_height(label,label_width,
        layout.small_font,measurer,14.f*s);
    const float value_height=measured_height(row_value,value_width,
        value_font,measurer,value_line_height);
    layout.items.push_back({0.f,row_y,label_width,label_height,std::move(label),
                            {163,193,214,255},layout.small_font});
    layout.items.push_back({value_x,row_y,value_width,value_height,
                            std::move(row_value),value_color,value_font});
    y+=std::max(20.f*s,std::max(label_height,value_height)+6.f*s);
  };
  for(const auto& fact:value.facts)
    add_row(fact.label,fact.value,
            fact.positive?Color{105,213,244,255}:Color{235,244,255,255},
            layout.body_font,18.f*s);
  y += 5.f*s;
  layout.items.push_back({0.f,y,layout.clip.width,14.f*s,
                          value.own_settlements.empty()?"SETTLEMENT INTELLIGENCE":"OWN SETTLEMENTS",
                          {105,213,244,255},layout.small_font});
  y += 25.f*s;
  for(const auto& colony:value.own_settlements){
    add_row("Settlement",colony.name,{235,244,255,255},layout.small_font,16.f*s);
    add_row("Body",colony.body,{235,244,255,255},layout.small_font,16.f*s);
    add_row("Population",number(colony.population_millions)+"M",{235,244,255,255},layout.small_font,16.f*s);
    add_row("Infrastructure",number(colony.infrastructure,2),{235,244,255,255},layout.small_font,16.f*s);
    add_row("Stability",number(colony.stability*100.,0)+"%",{235,244,255,255},layout.small_font,16.f*s);
    y+=16.f*s;
  }
  add_block(value.foreign_settlement_intelligence,0.f,layout.clip.width,
            layout.small_font,15.f*s,{163,193,214,255});
  layout.height=y+8.f*s;
  return layout;
}

std::optional<UiRect> intersection(UiRect first,UiRect second){
  const float left=std::max(first.x,second.x),top=std::max(first.y,second.y);
  const float right=std::min(first.x+first.width,second.x+second.width);
  const float bottom=std::min(first.y+first.height,second.y+second.height);
  if(right<=left||bottom<=top)return std::nullopt;
  return UiRect{left,top,right-left,bottom-top};
}

float maximum_scroll(const SystemInspection& value, UiRect bounds,
    const std::function<TextExtent(const Text&)>& measurer) {
  const auto layout = content_layout(value, bounds,measurer);
  return std::max(0.f, layout.height - layout.clip.height);
}

void clamp_scroll(float& scroll, const SystemInspection& value, UiRect bounds,
    const std::function<TextExtent(const Text&)>& measurer) {
  scroll = std::clamp(scroll,0.f,maximum_scroll(value,bounds,measurer));
}
}

SystemInspection build_system_inspection(const FreshCampaignState& state, int selected_system_id) {
  SystemInspection result; result.selected_system_id = selected_system_id; result.observer_id = state.player_civilization_id;
  const auto observer = std::ranges::find(state.civilizations, state.player_civilization_id, &Civilization::id);
  if (observer == state.civilizations.end() || !observer->is_player) { result.name = "INTELLIGENCE UNAVAILABLE"; result.survey_status = "Observer unavailable"; result.guidance = "Player identity could not be validated."; return result; }
  const auto system = std::ranges::find(state.systems, selected_system_id, &StellarSystem::id);
  if (system == state.systems.end()) { result.name = selected_system_id < 0 ? "SELECT A STAR" : "TARGET LOST"; result.survey_status = "Unavailable"; result.guidance = "Select an available system."; return result; }
  const auto level = state.knowledge.system_survey_level(observer->id, system->id);
  result.survey_progress = std::clamp(state.knowledge.system_survey_progress(observer->id, system->id), 0., 1.);
  result.survey_status = level == SystemSurveyLevel::unknown ? "Unknown" : level == SystemSurveyLevel::detected ? "Detected" : level == SystemSurveyLevel::partially_surveyed ? "Survey in progress" : "Fully surveyed";
  const auto home = std::ranges::find(state.systems, observer->home_system_id, &StellarSystem::id);
  result.facts.push_back({"DISTANCE FROM HOMEWORLD", home == state.systems.end() ? "Unknown" : format_interstellar_metric_primary(distance_light_years(home->position, system->position)), home != state.systems.end()});
  if (level == SystemSurveyLevel::unknown) { result.name = "UNKNOWN"; result.guidance = "Dispatch a scout or science vessel to establish local information."; return result; }
  if (level != SystemSurveyLevel::fully_surveyed) {
    result.name = system->name;
    result.guidance = level == SystemSurveyLevel::detected
        ? "Planet, resource, anomaly and civilization data remain unknown. Send a scout for reconnaissance or a science vessel for a detailed survey."
        : "Detailed survey is incomplete. A science vessel must finish its work before planetary measurements are confirmed.";
    return result;
  }
  result.name = system->name;
  result.guidance = "Survey complete. Review stellar findings and owned settlements below.";
  result.facts.push_back({"PRIMARY STAR", star_label(system->primary), true});
  result.facts.push_back({"SYSTEM TRAITS", archetype_label(system->archetype), true});
  result.facts.push_back({"HABITABLE WORLD", system->has_habitable_world ? "Yes" : "No", system->has_habitable_world});
  result.facts.push_back({"ANOMALY", system->has_anomaly ? "Yes" : "No", system->has_anomaly});
  result.facts.push_back({"RARE RESOURCES", system->has_rare_resource ? "Yes" : "No", system->has_rare_resource});
  result.facts.push_back({"PRE-WARP LIFE", system->has_pre_warp_civilization ? "Yes" : "No", system->has_pre_warp_civilization});
  std::vector<const Colony*> own_colonies;
  for (const auto& colony : state.colonies)
    if (colony.system_id == system->id && colony.civilization_id == observer->id)
      own_colonies.push_back(&colony);
  std::ranges::sort(own_colonies, [](const Colony* a, const Colony* b) {
    return std::tie(a->name, a->id) < std::tie(b->name, b->id);
  });
  for (const Colony* colony : own_colonies) {
    std::string body = "Unassigned body";
    if (colony->planetary_body_id)
      if (const auto found = std::ranges::find(state.bodies, *colony->planetary_body_id, &PlanetaryBody::id);
          found != state.bodies.end() && found->system_id == system->id)
        body = found->name;
    result.own_settlements.push_back({colony->id, colony->name, body,
      colony->population_millions, colony->infrastructure, colony->stability});
  }
  return result;
}

void SystemInspectionCard::set_text_measurer(
    std::function<TextExtent(const Text&)> value){
  text_measurer_=std::move(value);
  if(inspection_&&last_bounds_)
    clamp_scroll(scroll_,*inspection_,*last_bounds_,text_measurer_);
}
void SystemInspectionCard::set_inspection(SystemInspection value) {
  if (!inspection_ || inspection_->selected_system_id != value.selected_system_id || inspection_->observer_id != value.observer_id) {
    scroll_ = 0.f;
    pointer_owned_ = false;
  }
  inspection_ = std::move(value);
  if (last_bounds_) clamp_scroll(scroll_, *inspection_, *last_bounds_,text_measurer_);
}
void SystemInspectionCard::clear() noexcept { inspection_.reset(); scroll_ = 0.f; last_bounds_.reset(); pointer_owned_ = false; }
UiRect SystemInspectionCard::close_bounds(UiRect bounds) noexcept { const auto s=scale_for(bounds); return {bounds.x+bounds.width-30.f*s,bounds.y+7.f*s,24.f*s,24.f*s}; }
UiRect SystemInspectionCard::body_bounds(UiRect bounds) noexcept { const auto s=scale_for(bounds); return {bounds.x+10.f*s,bounds.y+62.f*s,std::max(0.f,bounds.width-20.f*s),std::max(0.f,bounds.height-70.f*s)}; }
InspectionHandleResult SystemInspectionCard::handle(const InputEvent& event, UiRect bounds) {
  if (!inspection_) return {};
  last_bounds_ = bounds;
  clamp_scroll(scroll_, *inspection_, bounds,text_measurer_);
  const auto close=close_bounds(bounds);
  if (event.type == InputEventType::LeftPressed && close.contains(event.position)) { clear(); return {true, true}; }
  if (event.type == InputEventType::PointerCancelled) { const bool captured=pointer_owned_; pointer_owned_=false; return {captured,false}; }
  if (pointer_owned_) {
    const bool pointer_event = event.type == InputEventType::PointerMove ||
        event.type == InputEventType::LeftPressed ||
        event.type == InputEventType::LeftReleased ||
        event.type == InputEventType::RightPressed ||
        event.type == InputEventType::RightReleased ||
        event.type == InputEventType::Wheel;
    if (!pointer_event) return {};
    if (event.type==InputEventType::LeftReleased||event.type==InputEventType::RightReleased)
      pointer_owned_=false;
    return {true,false};
  }
  if (!bounds.contains(event.position)) return {};
  if (event.type == InputEventType::LeftPressed || event.type == InputEventType::RightPressed) { pointer_owned_=true; return {true,false}; }
  if (event.type == InputEventType::Wheel) { scroll_ = std::clamp(scroll_ - event.wheel_y * 32.f*scale_for(bounds),0.f,maximum_scroll(*inspection_,bounds,text_measurer_)); return {true, false}; }
  const bool pointer_event = event.type == InputEventType::PointerMove ||
      event.type == InputEventType::LeftReleased ||
      event.type == InputEventType::RightReleased;
  return {pointer_event, false};
}
void SystemInspectionCard::render(DrawList& out, UiRect bounds) const {
  if (!inspection_) return;
  const auto& value = *inspection_; const auto layout=content_layout(value,bounds,text_measurer_); const float scale=layout.scale; const int body=layout.body_font; const int small=layout.small_font;
  last_bounds_=bounds; clamp_scroll(scroll_,value,bounds,text_measurer_);
  out.overlay.emplace_back(FilledRectangle{bounds, {7, 20, 35, 246}}); out.overlay.emplace_back(StrokedRectangle{bounds, {78, 168, 209, 255}});
  const UiRect header{bounds.x+10.f*scale,bounds.y+7.f*scale,std::max(0.f,bounds.width-20.f*scale),48.f*scale};
  text(out,{header.x,header.y,std::max(0.f,header.width-34.f*scale),21.f*scale},value.name,{235,244,255,255},body+4);
  text(out,{header.x,header.y+21.f*scale,std::max(0.f,header.width-68.f*scale),15.f*scale},value.survey_status,{105,213,244,255},small);
  text(out,{header.x+std::max(0.f,header.width-66.f*scale),header.y+21.f*scale,32.f*scale,15.f*scale},number(value.survey_progress*100.,0)+"%",{105,213,244,255},small);
  out.overlay.emplace_back(FilledRectangle{{header.x,header.y+40.f*scale,header.width,5.f*scale},{25,49,64,255}});
  out.overlay.emplace_back(FilledRectangle{{header.x,header.y+40.f*scale,header.width*static_cast<float>(std::clamp(value.survey_progress,0.,1.)),5.f*scale},{94,210,183,255}});
  for(const auto& item:layout.items){
    const UiRect row{layout.clip.x+item.x,
                     layout.clip.y+item.y-scroll_,item.width,item.height};
    const auto clipped=intersection(row,layout.clip);
    if(!clipped)continue;
    out.overlay.emplace_back(Text{{row.x,row.y},item.value,item.color,item.font,
                                  row.width,*clipped});
  }
  if(layout.height>layout.clip.height&&layout.clip.height>0.f){
    const float maximum=layout.height-layout.clip.height;
    const float thumb_height=std::min(layout.clip.height,std::max(18.f*scale,
        layout.clip.height*layout.clip.height/layout.height));
    const float travel=layout.clip.height-thumb_height;
    const float thumb_y=layout.clip.y+(maximum>0.f?travel*scroll_/maximum:0.f);
    const float indicator_width=3.f*scale;
    const float indicator_x=layout.clip.x+layout.clip.width-indicator_width;
    out.overlay.emplace_back(FilledRectangle{{indicator_x,layout.clip.y,
        indicator_width,layout.clip.height},{25,49,64,210}});
    out.overlay.emplace_back(FilledRectangle{{indicator_x,thumb_y,
        indicator_width,thumb_height},{105,213,244,230}});
  }
  const auto close=close_bounds(bounds);
  text(out,close,"X",{235,244,255,255},small);
}
} // namespace stellar::native_inspection
