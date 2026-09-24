#include "native_inspection.hpp"
#include <stellar/core/campaign_observation.hpp>

#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/interstellar_distance.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
std::string tr_at(const stellar::engine::LocalizationTable *table,
                  std::string_view key, std::string_view fallback) {
  if (table && table->contains(key))
    return std::string(table->translate(key));
  return std::string(fallback);
}
std::string trf_at(const stellar::engine::LocalizationTable *table,
                   std::string_view key,
                   std::initializer_list<std::string> args,
                   std::string_view fallback) {
  if (table && table->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return table->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}
std::string star_label(const std::optional<StellarClass>& value) {
  if (!value) return "Unknown";
  switch (*value) { case StellarClass::MRedDwarf:return "M-type red dwarf"; case StellarClass::KOrangeDwarf:return "K-type orange dwarf"; case StellarClass::GYellowDwarf:return "G-type yellow dwarf"; case StellarClass::FYellowWhiteDwarf:return "F-type yellow-white dwarf"; case StellarClass::AWhiteStar:return "A-type white star"; case StellarClass::HotBlueStar:return "Hot blue B/O star"; case StellarClass::Giant:return "Red/orange giant"; case StellarClass::WhiteDwarf:return "White dwarf"; case StellarClass::NeutronStar:return "Neutron star / pulsar"; case StellarClass::Pulsar:return "Pulsar"; case StellarClass::BlackHole:return "Black hole"; case StellarClass::Protostar:return "Young star / protostar"; }
  return "Legacy classification";
}
const char *star_key(const std::optional<StellarClass>& value) {
  if (!value) return "INSPECTION_STAR_UNKNOWN";
  switch (*value) { case StellarClass::MRedDwarf:return "INSPECTION_STAR_M"; case StellarClass::KOrangeDwarf:return "INSPECTION_STAR_K"; case StellarClass::GYellowDwarf:return "INSPECTION_STAR_G"; case StellarClass::FYellowWhiteDwarf:return "INSPECTION_STAR_F"; case StellarClass::AWhiteStar:return "INSPECTION_STAR_A"; case StellarClass::HotBlueStar:return "INSPECTION_STAR_BO"; case StellarClass::Giant:return "INSPECTION_STAR_GIANT"; case StellarClass::WhiteDwarf:return "INSPECTION_STAR_WHITE_DWARF"; case StellarClass::NeutronStar:return "INSPECTION_STAR_NEUTRON"; case StellarClass::Pulsar:return "INSPECTION_STAR_PULSAR"; case StellarClass::BlackHole:return "INSPECTION_STAR_BLACK_HOLE"; case StellarClass::Protostar:return "INSPECTION_STAR_PROTOSTAR"; }
  return "INSPECTION_STAR_LEGACY";
}
std::string archetype_label(StarArchetype value) { switch(value) { case StarArchetype::Standard:return "Standard"; case StarArchetype::ResourceRich:return "Resource rich"; case StarArchetype::HabitableRich:return "Habitable rich"; case StarArchetype::BarrenFrontier:return "Barren frontier"; case StarArchetype::Nebula:return "Nebula"; case StarArchetype::NeutronPulsar:return "Neutron pulsar"; case StarArchetype::BlackHole:return "Black hole"; case StarArchetype::AncientRuin:return "Ancient ruins"; case StarArchetype::Dangerous:return "Dangerous"; case StarArchetype::Legendary:return "Legendary"; } return "Unknown"; }
const char *archetype_key(StarArchetype value) { switch(value) { case StarArchetype::Standard:return "INSPECTION_ARCH_STANDARD"; case StarArchetype::ResourceRich:return "INSPECTION_ARCH_RESOURCE"; case StarArchetype::HabitableRich:return "INSPECTION_ARCH_HABITABLE"; case StarArchetype::BarrenFrontier:return "INSPECTION_ARCH_BARREN"; case StarArchetype::Nebula:return "INSPECTION_ARCH_NEBULA"; case StarArchetype::NeutronPulsar:return "INSPECTION_ARCH_NEUTRON"; case StarArchetype::BlackHole:return "INSPECTION_ARCH_BLACK_HOLE"; case StarArchetype::AncientRuin:return "INSPECTION_ARCH_RUIN"; case StarArchetype::Dangerous:return "INSPECTION_ARCH_DANGEROUS"; case StarArchetype::Legendary:return "INSPECTION_ARCH_LEGENDARY"; } return "INSPECTION_STAR_UNKNOWN"; }

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
    const std::function<TextExtent(const Text&)>& measurer,
    const stellar::engine::LocalizationTable *locale = nullptr) {
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return tr_at(locale, key, fallback);
  };
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
  layout.items.push_back({0.f,y,layout.clip.width,14.f*s,
                          tr("INSPECTION_SURVEY_FINDINGS", "SURVEY FINDINGS"),
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
                          tr(value.developer_inspection?"INSPECTION_ALL_SETTLEMENTS":value.own_settlements.empty()?"INSPECTION_SETTLEMENT_INTEL":"INSPECTION_OWN_SETTLEMENTS",
                             value.developer_inspection?"ALL SETTLEMENTS":value.own_settlements.empty()?"SETTLEMENT INTELLIGENCE":"OWN SETTLEMENTS"),
                          {105,213,244,255},layout.small_font});
  y += 25.f*s;
  for(const auto& colony:value.own_settlements){
    add_row(tr("INSPECTION_SETTLEMENT","Settlement"),colony.name,{235,244,255,255},layout.small_font,16.f*s);
    add_row(tr("INSPECTION_BODY","Body"),colony.body,{235,244,255,255},layout.small_font,16.f*s);
    add_row(tr("INSPECTION_POPULATION","Population"),number(colony.population_millions)+"M",{235,244,255,255},layout.small_font,16.f*s);
    add_row(tr("INSPECTION_INFRASTRUCTURE","Infrastructure"),number(colony.infrastructure,2),{235,244,255,255},layout.small_font,16.f*s);
    add_row(tr("INSPECTION_STABILITY","Stability"),number(colony.stability*100.,0)+"%",{235,244,255,255},layout.small_font,16.f*s);
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
    const std::function<TextExtent(const Text&)>& measurer,
    const stellar::engine::LocalizationTable *locale = nullptr) {
  const auto layout = content_layout(value, bounds,measurer,locale);
  return std::max(0.f, layout.height - layout.clip.height);
}

void clamp_scroll(float& scroll, const SystemInspection& value, UiRect bounds,
    const std::function<TextExtent(const Text&)>& measurer,
    const stellar::engine::LocalizationTable *locale = nullptr) {
  scroll = std::clamp(scroll,0.f,maximum_scroll(value,bounds,measurer,locale));
}
}

SystemInspection build_system_inspection(const FreshCampaignState& state, int selected_system_id,
                                         const stellar::engine::LocalizationTable *locale) {
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return tr_at(locale, key, fallback);
  };
  SystemInspection result; result.selected_system_id = selected_system_id; result.observer_id = state.player_civilization_id;
  const auto observer = std::ranges::find(state.civilizations, state.player_civilization_id, &Civilization::id);
  if (observer == state.civilizations.end() || !observer->is_player) { result.name = tr("INSPECTION_INTEL_UNAVAILABLE","INTELLIGENCE UNAVAILABLE"); result.survey_status = tr("INSPECTION_OBSERVER_UNAVAILABLE","Observer unavailable"); result.guidance = tr("INSPECTION_IDENTITY_FAILED","Player identity could not be validated."); return result; }
  const auto system = std::ranges::find(state.systems, selected_system_id, &StellarSystem::id);
  if (system == state.systems.end()) { result.name = tr(selected_system_id < 0 ? "INSPECTION_SELECT_STAR" : "INSPECTION_TARGET_LOST", selected_system_id < 0 ? "SELECT A STAR" : "TARGET LOST"); result.survey_status = tr("INSPECTION_UNAVAILABLE","Unavailable"); result.guidance = tr("INSPECTION_SELECT_GUIDANCE","Select an available system."); return result; }
  const auto level = observation_survey_level(state, observer->id, system->id);
  const bool developer = developer_observation(state, observer->id);
  result.developer_inspection = developer;
  result.survey_progress = developer ? 1. : std::clamp(state.knowledge.system_survey_progress(observer->id, system->id), 0., 1.);
  result.survey_status = tr(level == SystemSurveyLevel::unknown ? "INSPECTION_SURVEY_UNKNOWN" : level == SystemSurveyLevel::detected ? "INSPECTION_SURVEY_DETECTED" : level == SystemSurveyLevel::partially_surveyed ? "INSPECTION_SURVEY_PARTIAL" : "INSPECTION_SURVEY_FULL",
                            level == SystemSurveyLevel::unknown ? "Unknown" : level == SystemSurveyLevel::detected ? "Detected" : level == SystemSurveyLevel::partially_surveyed ? "Survey in progress" : "Fully surveyed");
  const auto home = std::ranges::find(state.systems, observer->home_system_id, &StellarSystem::id);
  result.facts.push_back({tr("INSPECTION_DISTANCE","DISTANCE FROM HOMEWORLD"), home == state.systems.end() ? tr("INSPECTION_UNKNOWN","Unknown") : format_interstellar_metric_primary(distance_light_years(home->position, system->position)), home != state.systems.end()});
  if (level == SystemSurveyLevel::unknown) { result.name = tr("INSPECTION_NAME_UNKNOWN","UNKNOWN"); result.guidance = tr("INSPECTION_SCOUT_GUIDANCE","Dispatch a scout or science vessel to establish local information."); return result; }
  if (level != SystemSurveyLevel::fully_surveyed) {
    result.name = system->name;
    result.guidance = tr(level == SystemSurveyLevel::detected
        ? "INSPECTION_DETECTED_GUIDANCE"
        : "INSPECTION_PARTIAL_GUIDANCE",
        level == SystemSurveyLevel::detected
        ? "Planet, resource, anomaly and civilization data remain unknown. Send a scout for reconnaissance or a science vessel for a detailed survey."
        : "Detailed survey is incomplete. A science vessel must finish its work before planetary measurements are confirmed.");
    return result;
  }
  result.name = system->name;
  result.guidance = tr(developer ? "INSPECTION_DEVELOPER_GUIDANCE" : "INSPECTION_SURVEYED_GUIDANCE", developer ? "Developer inspection · live stellar and settlement statistics." : "Survey complete. Review stellar findings and owned settlements below.");
  if (developer) result.foreign_settlement_intelligence = tr("INSPECTION_DEVELOPER_SETTLEMENTS","Developer access: all settlements shown with actual population and development.");
  result.facts.push_back({tr("INSPECTION_PRIMARY_STAR","PRIMARY STAR"), system->stellar_object?stellar_object_definition(system->stellar_object->type).name:tr(star_key(system->primary),star_label(system->primary)), true});
  if(system->stellar_object){
    const auto& p=*system->stellar_object;
    result.facts.push_back({tr("INSPECTION_RADIUS","STELLAR RADIUS"),number(p.radius_solar*695700.,0)+" km",true});
    result.facts.push_back({tr("INSPECTION_LUMINOSITY","LUMINOSITY"),number(p.luminosity_solar,4)+" x Sol",true});
    result.facts.push_back({tr("INSPECTION_SAFE_APPROACH","SAFE APPROACH"),number(p.safe_approach_au*astronomical_unit_km,0)+" km",false});
    if(p.hooks.is_rare_discovery)result.facts.push_back({tr("INSPECTION_DISCOVERY","DISCOVERY"),p.hooks.rarity_tier,true});
    if(p.jet_half_angle_radians>0)result.facts.push_back({tr("INSPECTION_HAZARD","STELLAR HAZARD"),tr("INSPECTION_HAZARD_JETS","Directional high-energy jets"),false});
  }
  result.facts.push_back({tr("INSPECTION_SYSTEM_TRAITS","SYSTEM TRAITS"), tr(archetype_key(system->archetype),archetype_label(system->archetype)), true});
  result.facts.push_back({tr("INSPECTION_HABITABLE","HABITABLE WORLD"), tr(system->has_habitable_world ? "INSPECTION_YES" : "INSPECTION_NO", system->has_habitable_world ? "Yes" : "No"), system->has_habitable_world});
  result.facts.push_back({tr("INSPECTION_ANOMALY","ANOMALY"), tr(system->has_anomaly ? "INSPECTION_YES" : "INSPECTION_NO", system->has_anomaly ? "Yes" : "No"), system->has_anomaly});
  result.facts.push_back({tr("INSPECTION_RARE","RARE RESOURCES"), tr(system->has_rare_resource ? "INSPECTION_YES" : "INSPECTION_NO", system->has_rare_resource ? "Yes" : "No"), system->has_rare_resource});
  result.facts.push_back({tr("INSPECTION_PREWARP","PRE-WARP LIFE"), tr(system->has_pre_warp_civilization ? "INSPECTION_YES" : "INSPECTION_NO", system->has_pre_warp_civilization ? "Yes" : "No"), system->has_pre_warp_civilization});
  std::vector<const Colony*> own_colonies;
  for (const auto& colony : state.colonies)
    if (colony.system_id == system->id && can_inspect_settlement(state, observer->id, colony))
      own_colonies.push_back(&colony);
  std::ranges::sort(own_colonies, [](const Colony* a, const Colony* b) {
    return std::tie(a->name, a->id) < std::tie(b->name, b->id);
  });
  for (const Colony* colony : own_colonies) {
    std::string body = tr("INSPECTION_UNASSIGNED_BODY","Unassigned body");
    if (colony->planetary_body_id)
      if (const auto found = std::ranges::find(state.bodies, *colony->planetary_body_id, &PlanetaryBody::id);
          found != state.bodies.end() && found->system_id == system->id)
        body = found->name;
    std::string label = colony->name;
    if (developer) {
      const auto owner = std::ranges::find(state.civilizations, colony->civilization_id, &Civilization::id);
      if (owner != state.civilizations.end()) label += " · " + owner->name;
    }
    result.own_settlements.push_back({colony->id, label, body,
      colony->population_millions, colony->infrastructure, colony->stability});
  }
  return result;
}

void SystemInspectionCard::set_text_measurer(
    std::function<TextExtent(const Text&)> value){
  text_measurer_=std::move(value);
  if(inspection_&&last_bounds_)
    clamp_scroll(scroll_,*inspection_,*last_bounds_,text_measurer_,locale_);
}
void SystemInspectionCard::set_inspection(SystemInspection value) {
  if (!inspection_ || inspection_->selected_system_id != value.selected_system_id || inspection_->observer_id != value.observer_id) {
    scroll_ = 0.f;
    pointer_owned_ = false;
  }
  inspection_ = std::move(value);
  if (last_bounds_) clamp_scroll(scroll_, *inspection_, *last_bounds_,text_measurer_,locale_);
}
void SystemInspectionCard::clear() noexcept { inspection_.reset(); scroll_ = 0.f; last_bounds_.reset(); pointer_owned_ = false; focus_ = -1; }
UiRect SystemInspectionCard::close_bounds(UiRect bounds) noexcept { const auto s=scale_for(bounds); return {bounds.x+bounds.width-30.f*s,bounds.y+7.f*s,24.f*s,24.f*s}; }
UiRect SystemInspectionCard::body_bounds(UiRect bounds) noexcept { const auto s=scale_for(bounds); return {bounds.x+10.f*s,bounds.y+62.f*s,std::max(0.f,bounds.width-20.f*s),std::max(0.f,bounds.height-70.f*s)}; }
InspectionHandleResult SystemInspectionCard::handle(const InputEvent& event, UiRect bounds) {
  if (!inspection_) { focus_ = -1; return {}; }
  last_bounds_ = bounds;
  clamp_scroll(scroll_, *inspection_, bounds,text_measurer_,locale_);
  const auto close=close_bounds(bounds);
  if (event.type == InputEventType::LeftPressed || event.type == InputEventType::PointerCancelled) focus_ = -1;
  if (event.type == InputEventType::KeyPressed && event.key) {
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u,kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u,kHome=0x4000004au,kEnd=0x4000004du;
    if (event.key==kTab||event.key==kRight||event.key==kLeft||event.key==kDown||event.key==kUp||event.key==kHome||event.key==kEnd) { focus_=0; return {true,false}; }
    if ((event.key==kReturn||event.key==kSpace)&&focus_>=0)
      return handle({InputEventType::LeftPressed,{close.x+close.width*.5f,close.y+close.height*.5f}},bounds);
    return {};
  }
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
  if (event.type == InputEventType::Wheel) { scroll_ = std::clamp(scroll_ - event.wheel_y * 32.f*scale_for(bounds),0.f,maximum_scroll(*inspection_,bounds,text_measurer_,locale_)); return {true, false}; }
  const bool pointer_event = event.type == InputEventType::PointerMove ||
      event.type == InputEventType::LeftReleased ||
      event.type == InputEventType::RightReleased;
  return {pointer_event, false};
}
void SystemInspectionCard::render(DrawList& out, UiRect bounds) const {
  if (!inspection_) return;
  const auto& value = *inspection_; const auto layout=content_layout(value,bounds,text_measurer_,locale_); const float scale=layout.scale; const int body=layout.body_font; const int small=layout.small_font;
  last_bounds_=bounds; clamp_scroll(scroll_,value,bounds,text_measurer_,locale_);
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
  if(focus_>=0)out.overlay.emplace_back(StrokedRectangle{close,{164,221,237,255}});
}
} // namespace stellar::native_inspection
