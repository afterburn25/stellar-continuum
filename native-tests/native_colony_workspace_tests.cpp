#include "native_colony_workspace.hpp"
#include "native_ui_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <utility>

namespace {
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_map;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Gate129 check failed at line " +
                             std::to_string(line) + ": " +
                             std::string(expression));
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

[[nodiscard]] Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}
[[nodiscard]] bool contains(UiRect outer, UiRect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}
[[nodiscard]] bool overlaps(UiRect left, UiRect right) {
  return left.x < right.x + right.width &&
         right.x < left.x + left.width &&
         left.y < right.y + right.height &&
         right.y < left.y + left.height;
}

NativeSurfaceSite site(int id, bool complete = false) {
  NativeSurfaceSite result;
  result.building_id = id;
  result.type_id = "power_generator";
  result.name = "Power Generator With A Long Authored Surface Name " +
                std::to_string(id);
  result.industry_progress = complete ? 300.0 : id * 10.0;
  result.industry_cost = 300.0;
  result.progress_fraction = complete ? 1.0 : id / 20.0;
  result.complete = complete;
  result.powered = complete;
  result.staffed = complete;
  result.enabled = true;
  result.condition = .93;
  result.efficiency = .86;
  result.construction_stage = "Structural assembly";
  result.remaining_construction_materials =
      result.industry_cost - result.industry_progress;
  return result;
}

NativeColonyView view(std::uint64_t generation = 1) {
  NativeColonyView result;
  result.campaign_generation = generation;
  result.revision = 4;
  result.player_civilization_id = 7;
  result.system_id = 2;
  result.body_id = 9;
  result.colony_id = 12;
  result.colony_name = "New Horizon";
  result.body_display_name = "Kepler Prospect";
  result.population_species_id = "Terran";
  result.formatted_treasury = "$1.2B UED";
  result.currency = {"United Earth Dollar", "UED", "$", 10'000'000.0};
  result.treasury_budget_units = 120.0;
  result.stored_industry = 440.0;
  result.population_millions = 8'200.0;
  result.infrastructure = .81;
  result.stability = .74;
  result.working_age_population_millions = 5'000.0;
  result.employed_population_millions = 4'400.0;
  result.unemployed_population_millions = 600.0;
  result.employment_rate = .88;
  result.workforce_available_millions = 4'400.0;
  result.workforce_demand_millions = 3'600.0;
  result.food_capacity_millions = 9'000.0;
  result.water_capacity_millions = 8'800.0;
  result.housing_capacity_millions = 10'000.0;
  result.supported_population_millions = 8'800.0;
  result.sustenance_support_ratio = 1.0;
  result.limiting_sustenance_supply = "water";
  result.food_reserve_days = 18.0;
  result.water_reserve_days = 5.5;
  result.surface_hub_level = 2;
  result.building_capacity = 32;
  result.power_supply = 12.0;
  result.power_demand = 8.0;
  result.stored_power_days = 4.0;
  result.credits_per_day = .13;
  result.upkeep_credits_per_day = .08;
  result.industry_per_day = 3.5;
  result.science_per_day = 2.0;
  result.active_research_facilities = 2;
  result.active_research_lab_units = 3.5;
  result.required_habitat_systems = 1;
  result.specialization_name = "Balanced settlement";
  result.specialization_description = "No dominant surface specialization";
  result.construction_sites = {site(1, true), site(2)};
  return result;
}

ColonyWorkspaceCommand click_label(NativeColonyWorkspace& workspace,std::string_view label,int width,int height){
  for(int attempt=0;attempt<30;++attempt){
    DrawList draw;workspace.render(draw,width,height);
    for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item);t&&t->value==label){
      Point p{t->at.x+12,t->at.y+3};if(t->clip&&t->clip->contains(p)){
        (void)workspace.handle({InputEventType::LeftPressed,p},width,height);
        return workspace.handle({InputEventType::LeftReleased,p},width,height);
      }
    }
    (void)workspace.handle({InputEventType::Wheel,center(PlanetaryLayout::make(width,height).details),{},-2.f},width,height);
  }
  throw std::runtime_error("Unreachable planetary action: "+std::string(label));
}

void only_planetary_screen_is_rendered(){
  for(const auto [width,height]:{std::pair{1280,720},std::pair{1920,1080},std::pair{3840,2160}}){
    NativeColonyWorkspace workspace;workspace.open(view());
    DrawList draw;workspace.render(draw,width,height);
    REQUIRE(std::ranges::any_of(draw.overlay,[](const auto& item){return std::holds_alternative<Scene3DView>(item);}));
    REQUIRE(std::ranges::none_of(draw.overlay,[](const auto& item){const auto* t=std::get_if<Text>(&item);return t&&(t->value=="SURFACE MODULES & CONSTRUCTION"||t->value=="Open Surface");}));
    (void)click_label(workspace,"Build",width,height);
    (void)click_label(workspace,"Available slot",width,height);
    auto command=workspace.handle({InputEventType::EscapePressed},width,height);
    REQUIRE(workspace.visible()); // backs out of the slot first
    command=workspace.handle({InputEventType::EscapePressed},width,height);
    REQUIRE(command.kind==ColonyWorkspaceCommandKind::Close&&!workspace.visible());
    workspace.open(view());workspace.discard_campaign();REQUIRE(!workspace.visible()&&!workspace.view());
  }
}

void freight_review_input_and_layout() {
  for (const auto size : {Point{1280,720},Point{1920,1080},Point{3840,2160}}) {
    const auto width=static_cast<int>(size.x),height=static_cast<int>(size.y);
    const auto layout=ColonyWorkspaceLayout::for_viewport(width,height);
    REQUIRE(contains(layout.surface,layout.freight_review));
    REQUIRE(contains(layout.freight_review,layout.freight_text));
    REQUIRE(contains(layout.freight_review,layout.freight_confirm));
    REQUIRE(!overlaps(layout.freight_text,layout.freight_confirm));
    REQUIRE(!overlaps(layout.freight_cancel,layout.freight_confirm));
    NativeColonyWorkspace workspace;auto owned=view();owned.resource_outpost=true;
    workspace.open(owned);
    const auto click=[&](UiRect bounds){const auto point=center(bounds);(void)workspace.handle({InputEventType::LeftPressed,point},width,height);return workspace.handle({InputEventType::LeftReleased,point},width,height);};
    (void)click_label(workspace,"Economy",width,height);
    REQUIRE(click_label(workspace,"Collect materials",width,height).kind==ColonyWorkspaceCommandKind::ReviewFreight);
    NativeOutpostFreightPreview quote;quote.campaign_generation=owned.campaign_generation;quote.revision=19;
    quote.player_civilization_id=owned.player_civilization_id;quote.colony_id=owned.colony_id;
    quote.body_id=owned.body_id;quote.system_id=owned.system_id;quote.accepted=true;
    quote.fleet_name="Mercury Freight";quote.home_name="Earth";quote.outpost_name="Mining Depot";
    quote.cargo_capacity=1000;quote.stored_materials=100;quote.extraction_per_day=2;
    workspace.set_freight_preview(quote);REQUIRE(workspace.freight_preview().has_value());
    // A drag across different buttons must not dispatch.
    (void)workspace.handle({InputEventType::LeftPressed,center(layout.freight_cancel)},width,height);
    REQUIRE(workspace.handle({InputEventType::LeftReleased,center(layout.freight_confirm)},width,height).kind==ColonyWorkspaceCommandKind::None);
    const auto confirmed=click(layout.freight_confirm);
    REQUIRE(confirmed.kind==ColonyWorkspaceCommandKind::ConfirmFreight&&confirmed.quote_revision==19);
    REQUIRE(click(layout.freight_cancel).kind==ColonyWorkspaceCommandKind::CancelFreight);
    REQUIRE(!workspace.freight_preview()&&workspace.visible());
    quote.accepted=false;quote.message="No idle freighter available.";workspace.set_freight_preview(quote);
    REQUIRE(click(layout.freight_confirm).kind==ColonyWorkspaceCommandKind::None);
    DrawList denied;workspace.render(denied,width,height);
    REQUIRE(std::ranges::none_of(denied.overlay,[](const auto& item){const auto* t=std::get_if<Text>(&item);return t&&t->value=="DISPATCH FREIGHTER";}));
    REQUIRE(workspace.handle({InputEventType::EscapePressed},width,height).kind==ColonyWorkspaceCommandKind::CancelFreight);
    REQUIRE(workspace.visible()&&!workspace.freight_preview());
    quote.accepted=true;workspace.set_freight_preview(quote);
    (void)workspace.handle({InputEventType::PointerCancelled},width,height);REQUIRE(!workspace.freight_preview());
    workspace.set_freight_preview(quote);auto changed=owned;changed.player_civilization_id++;
    workspace.set_view(changed);REQUIRE(!workspace.freight_preview());
    workspace.open(owned);workspace.set_freight_preview(quote);workspace.close();REQUIRE(!workspace.freight_preview());
  }
}

void keyboard_focus() {
  constexpr std::uint32_t kTab=9u,kReturn=13u,kHome=0x4000004au,kEnd=0x4000004du;
  const int width=1280,height=720;
  NativeColonyWorkspace workspace;auto owned=view();owned.resource_outpost=true;
  workspace.open(owned);
  const auto key=[&](std::uint32_t k,bool shift=false){InputEvent e{InputEventType::KeyPressed};e.key=k;e.shift=shift;return workspace.handle(e,width,height);};
  NativeOutpostFreightPreview quote;quote.campaign_generation=owned.campaign_generation;quote.revision=19;
  quote.player_civilization_id=owned.player_civilization_id;quote.colony_id=owned.colony_id;
  quote.body_id=owned.body_id;quote.system_id=owned.system_id;quote.accepted=true;
  workspace.set_freight_preview(quote);
  REQUIRE(workspace.focus()<0);
  REQUIRE(workspace.focused_label().empty());
  (void)key(kTab);REQUIRE(workspace.focus()==0);
  REQUIRE(workspace.focused_label()=="Cancel");
  (void)key(kTab);REQUIRE(workspace.focus()==1);
  REQUIRE(workspace.focused_label()=="Confirm dispatch");
  (void)key(kTab,true);REQUIRE(workspace.focus()==0);
  (void)key(kEnd);REQUIRE(workspace.focus()==1);
  const auto confirmed=key(kReturn);
  REQUIRE(confirmed.kind==ColonyWorkspaceCommandKind::ConfirmFreight&&confirmed.quote_revision==19);
  REQUIRE(workspace.freight_preview().has_value()&&workspace.focus()==1);
  (void)key(kHome);const auto cancelled=key(kReturn);
  REQUIRE(cancelled.kind==ColonyWorkspaceCommandKind::CancelFreight&&!workspace.freight_preview()&&workspace.focus()<0);
  quote.accepted=false;quote.message="No idle freighter available.";workspace.set_freight_preview(quote);
  (void)key(kEnd);REQUIRE(workspace.focus()==0);
  REQUIRE(key(kReturn).kind==ColonyWorkspaceCommandKind::CancelFreight);
  workspace.set_freight_preview(quote);
  (void)workspace.handle({InputEventType::LeftPressed,center(ColonyWorkspaceLayout::for_viewport(width,height).freight_cancel)},width,height);
  REQUIRE(workspace.focus()<0);
  (void)workspace.handle({InputEventType::PointerCancelled},width,height);
}

void planetary_ring_delegates() {
  constexpr std::uint32_t kTab=9u;
  const int width=1280,height=720;
  NativeColonyWorkspace workspace;workspace.open(view());
  DrawList draw;workspace.render(draw,width,height);
  const auto key=[&](std::uint32_t k){InputEvent e{InputEventType::KeyPressed};e.key=k;return workspace.handle(e,width,height);};
  // Outside the freight modal the workspace exposes the planetary screen's
  // hit-registry ring.
  REQUIRE(workspace.focus()<0);
  REQUIRE(workspace.focused_label().empty());
  REQUIRE(!workspace.focused_bounds(width,height).has_value());
  (void)key(kTab);REQUIRE(workspace.focus()==0);
  REQUIRE(!workspace.focused_label().empty());
  REQUIRE(workspace.focused_bounds(width,height).has_value());
  REQUIRE(workspace.focused_control()==stellar::engine::AnnouncementControl::Button);
  // Opening the freight modal releases the planetary ring; the modal's own
  // ring then reports.
  auto owned=view();owned.resource_outpost=true;
  NativeOutpostFreightPreview quote;quote.campaign_generation=owned.campaign_generation;
  quote.player_civilization_id=owned.player_civilization_id;quote.colony_id=owned.colony_id;
  quote.body_id=owned.body_id;quote.system_id=owned.system_id;quote.accepted=true;
  workspace.set_view(owned);workspace.set_freight_preview(quote);
  REQUIRE(workspace.focus()<0);
  (void)key(kTab);REQUIRE(workspace.focus()==0);
  REQUIRE(workspace.focused_label()=="Cancel");
}

} // namespace
int main(){try{only_planetary_screen_is_rendered();freight_review_input_and_layout();keyboard_focus();planetary_ring_delegates();
  std::cout<<"Planetary workspace and freight checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
