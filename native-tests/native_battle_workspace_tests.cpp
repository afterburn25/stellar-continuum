#include "native_battle_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::core;
using namespace stellar::native_battle_ui;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

InputEvent press(InputEventType type, Point at) {
  InputEvent event{};
  event.type = type;
  event.position = at;
  return event;
}

void select_all_visible(NativeBattleWorkspace& workspace,int width,int height){
  Point first{static_cast<float>(width),static_cast<float>(height)}, last{};
  for(const auto& formation:workspace.snapshot()->formations){
    const auto point=workspace.project(formation.position,width,height);
    first.x=std::min(first.x,point.x-15.f);first.y=std::min(first.y,point.y-15.f);
    last.x=std::max(last.x,point.x+15.f);last.y=std::max(last.y,point.y+15.f);
  }
  (void)workspace.handle(press(InputEventType::LeftPressed,first),width,height);
  InputEvent drag=press(InputEventType::PointerMove,last);
  drag.delta={last.x-first.x,last.y-first.y};
  (void)workspace.handle(drag,width,height);
  (void)workspace.handle(press(InputEventType::LeftReleased,last),width,height);
}

MassiveObservedFormation formation(std::int64_t id, int civilization,
                                   MassivePoint at,
                                   std::string name = "Formation",
                                   int low = 40, int high = 60) {
  MassiveObservedFormation value;
  value.formation_id = id;
  value.civilization_id = civilization;
  value.display_name = std::move(name);
  value.position = at;
  value.velocity = {8.f, 0.f};
  value.shape = MassiveFormationShape::Wedge;
  value.ship_count_low = low;
  value.ship_count_high = high;
  value.strength_low = 120.f;
  value.strength_high = 240.f;
  return value;
}

MassiveCombatSnapshot snapshot() {
  MassiveCombatSnapshot value;
  value.battle_id = {1, 2, 3, 4};
  value.tick = 420;
  value.simulated_seconds = 42.;
  value.exact_own_ships = 100;
  value.formations.push_back(
      formation(11, 1, {-60.f, 0.f}, "Vanguard Fleet", 30, 30));
  value.formations.back().is_exact = true;
  value.formations.push_back(
      formation(12, 1, {-20.f, 40.f}, "Screen Wing", 20, 20));
  value.formations.back().is_exact = true;
  value.formations.push_back(
      formation(77, 9, {60.f, 10.f}, "Unidentified contact", 40, 90));
  value.events.push_back({.sequence = 1,
                          .tick = 400,
                          .type = MassiveCombatEventType::BeamVolley,
                          .actor_civilization_id = 1,
                          .actor_formation_id = 11,
                          .target_formation_id = 77,
                          .magnitude = 12,
                          .position = MassivePoint{10.f, 5.f},
                          .message = "Volley",
                          .details_known = true});
  value.active_missile_salvos.push_back(
      {.salvo_id = 5,
       .source_position = MassivePoint{-60.f, 0.f},
       .target_position = MassivePoint{60.f, 10.f},
       .current_position = MassivePoint{0.f, 4.f},
       .remaining_seconds = 8.f,
       .progress_01 = .5f,
       .count_low = 6,
       .count_high = 12,
       .incoming_to_own = false});
  return value;
}

void responsive_layout() {
  for (const auto [width, height] :
       {std::pair{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440},
        {3840,2160}}) {
    const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
    require(layout.surface.contains(center(layout.play)), "play escaped");
    require(layout.surface.contains(center(layout.speed)), "speed escaped");
    require(layout.surface.contains(center(layout.fit)), "fit escaped");
    require(layout.surface.contains(center(layout.menu)), "menu escaped");
    require(layout.surface.contains(center(layout.orders)),
            "orders escaped");
    require(layout.surface.contains(center(layout.status)),
            "status escaped");
    for (const auto &button : layout.order_buttons)
      require(layout.orders.contains(center(button)) ||
                  button.y < layout.orders.y + layout.orders.height,
              "order button drifted outside orders column");
  }
}

void open_fit_and_render() {
  NativeBattleWorkspace workspace;
  require(!workspace.visible(), "workspace visible before open");
  workspace.open(snapshot(), 1, 1280, 720);
  require(workspace.visible(), "workspace not visible after open");
  require(workspace.snapshot() && workspace.snapshot()->formations.size() == 3,
          "snapshot not retained");
  DrawList draw;
  workspace.render(draw, 1280, 720);
  require(workspace.rendered_tokens() > 0, "no formation tokens rendered");
  require(workspace.rendered_tokens() <= 4096,
          "token pool exceeded reference cap");
  require(!draw.overlay.empty(), "no overlay commands emitted");
  require(std::ranges::any_of(draw.overlay, [](const auto &value) {
    return std::holds_alternative<TriangleMesh>(value);
  }), "no ordered clipped tactical discs emitted");
  const auto background=std::ranges::find_if(draw.overlay,[](const auto&value){
    const auto*fill=std::get_if<FilledRectangle>(&value);
    return fill&&fill->color.a==255;
  });
  const auto first_disc=std::ranges::find_if(draw.overlay,[](const auto&value){
    return std::holds_alternative<TriangleMesh>(value);
  });
  require(background!=draw.overlay.end()&&first_disc!=draw.overlay.end()&&
              background<first_disc,
          "opaque battlefield background covers tactical discs");
  const auto field_top=BattleWorkspaceLayout::for_viewport(1280,720).top_row.y+
      BattleWorkspaceLayout::for_viewport(1280,720).top_row.height;
  for(const auto&item:draw.overlay)if(const auto*mesh=std::get_if<TriangleMesh>(&item)){
    require(mesh->clip&&mesh->clip->y==field_top,
            "tactical disc is not clipped to the ordered battlefield");
    require(std::ranges::all_of(mesh->vertices,[](Point point){
      return std::isfinite(point.x)&&std::isfinite(point.y);
    }),"tactical disc contains a nonfinite vertex");
  }
}

void selection_rules() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  const auto own = workspace.project({-60.f, 0.f}, width, height);
  const auto hostile = workspace.project({60.f, 10.f}, width, height);

  auto command = workspace.handle(press(InputEventType::LeftPressed, own),
                                  width, height);
  require(command.captured, "press not captured");
  command = workspace.handle(press(InputEventType::LeftReleased, own),
                             width, height);
  require(workspace.selection().contains(11), "own formation not selected");

  command = workspace.handle(press(InputEventType::LeftPressed, hostile),
                             width, height);
  command = workspace.handle(press(InputEventType::LeftReleased, hostile),
                             width, height);
  require(workspace.selection().empty(),
          "foreign formation must not enter the orderable selection");

  // Box selection captures only own formations.
  select_all_visible(workspace,width,height);
  require(workspace.selection().contains(11) &&
              workspace.selection().contains(12) &&
              !workspace.selection().contains(77),
          "box selection must cover own formations only");
}

void gesture_ownership() {
  constexpr int width=1280,height=720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(),1,width,height);
  const auto own=workspace.project({-60.f,0.f},width,height);
  const auto hostile=workspace.project({60.f,10.f},width,height);
  InputEvent hover{};hover.type=InputEventType::PointerMove;
  hover.position=hostile;hover.delta={400.f,300.f};
  auto command=workspace.handle(hover,width,height);
  DrawList hovered;
  workspace.render(hovered,width,height);
  require(std::ranges::any_of(hovered.overlay,[](const auto&item){
    const auto*label=std::get_if<Text>(&item);
    return label&&label->value.starts_with("Power ");
  }),"hover without a press did not expose formation detail");
  command=workspace.handle(press(InputEventType::LeftReleased,hostile),width,height);
  require(!command.captured&&workspace.selection().empty(),
          "hover or orphan left release began a selection gesture");
  command=workspace.handle(press(InputEventType::RightReleased,hostile),width,height);
  require(!command.captured&&command.orders.empty(),
          "orphan right release issued a context order");
  (void)workspace.handle(press(InputEventType::RightPressed,hostile),width,height);
  (void)workspace.handle(press(InputEventType::PointerCancelled,{}),width,height);
  command=workspace.handle(press(InputEventType::RightReleased,hostile),width,height);
  require(command.orders.empty(),"cancelled right gesture issued a context order");

  (void)workspace.handle(press(InputEventType::LeftPressed,own),width,height);
  InputEvent drag{};drag.type=InputEventType::PointerMove;
  drag.position=hostile;drag.delta={80.f,10.f};
  (void)workspace.handle(drag,width,height);
  (void)workspace.handle(press(InputEventType::PointerCancelled,{}),width,height);
  command=workspace.handle(press(InputEventType::LeftReleased,hostile),width,height);
  require(!command.captured&&workspace.selection().empty(),
          "focus loss allowed a cancelled selection to complete");

  const auto layout=BattleWorkspaceLayout::for_viewport(width,height);
  (void)workspace.handle(press(InputEventType::LeftPressed,center(layout.play)),width,height);
  command=workspace.handle(press(InputEventType::LeftReleased,own),width,height);
  require(command.orders.empty()&&workspace.selection().empty(),
          "chrome press released over the field selected or ordered a formation");

  (void)workspace.handle(press(InputEventType::LeftPressed,own),width,height);
  (void)workspace.handle(press(InputEventType::LeftReleased,own),width,height);
  (void)workspace.handle(press(InputEventType::LeftPressed,
      center(layout.order_buttons[2])),width,height);
  require(workspace.targeting(),"targeting was not armed for cancellation");
  (void)workspace.handle(press(InputEventType::PointerCancelled,{}),width,height);
  require(!workspace.targeting(),"focus loss retained a pending targeted order");
  command=workspace.handle(press(InputEventType::LeftReleased,hostile),width,height);
  require(command.orders.empty(),"cancelled targeting issued on orphan release");
}

void order_commands() {
  constexpr int width = 1280, height = 720;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);

  // Order button with no selection: warning, no command.
  auto command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.order_buttons[0])),
      width, height);
  require(command.kind == BattleWorkspaceCommandKind::None,
          "order issued without selection");

  const auto own = workspace.project({-60.f, 0.f}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  require(workspace.selection().contains(11), "selection failed");

  // Non-targeted order button emits an order for the selected formation.
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.order_buttons[0])),
      width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder,
          "non-targeted order missing");
  require(command.orders.size() == 1 && command.orders.front().formation_id == 11 &&
              command.orders.front().type == MassiveCombatOrderType::Hold,
          "non-targeted order payload wrong");
  command = workspace.handle(
      press(InputEventType::LeftReleased, center(layout.order_buttons[0])),
      width, height);
  require(workspace.selection().contains(11),
          "release over an order button must not clear the selection");

  // Targeted order enters the pick state, then resolves on release.
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.order_buttons[2])),
      width, height);
  require(workspace.targeting(), "targeting pick state not entered");
  command = workspace.handle(
      press(InputEventType::LeftReleased, center(layout.order_buttons[2])),
      width, height);
  require(workspace.targeting() &&
              command.kind == BattleWorkspaceCommandKind::None,
          "release over the armed order button must keep the pick state");
  const auto hostile = workspace.project({60.f, 10.f}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed,hostile),width,height);
  command = workspace.handle(press(InputEventType::LeftReleased, hostile),
                             width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder,
          "targeted order missing");
  require(command.orders.front().target_formation_id &&
              *command.orders.front().target_formation_id == 77,
          "targeted order must reference the picked formation");

  // Context order: right click on the hostile formation issues Engage.
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::RightPressed, hostile), width,
                         height);
  command = workspace.handle(press(InputEventType::RightReleased, hostile),
                             width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder &&
              command.orders.front().type == MassiveCombatOrderType::Engage &&
              command.orders.front().target_formation_id &&
              *command.orders.front().target_formation_id == 77,
          "context engage order wrong");

  // Right click on open space issues an Advance objective.
  (void)workspace.handle(press(InputEventType::RightPressed, {400.f, 300.f}),
                         width, height);
  command = workspace.handle(press(InputEventType::RightReleased, {400.f, 300.f}),
                             width, height);
  require(command.orders.front().type == MassiveCombatOrderType::Advance &&
              command.orders.front().objective,
          "context advance order must carry an objective");
  select_all_visible(workspace,width,height);
  command=workspace.handle(press(InputEventType::LeftPressed,center(layout.order_buttons[0])),width,height);
  require(command.orders.size()==2 && command.orders[0].formation_id==11 && command.orders[1].formation_id==12,
          "non-targeted batch included foreign formation or omitted owned formation");
  (void)workspace.handle(press(InputEventType::RightPressed,hostile),width,height);
  command=workspace.handle(press(InputEventType::RightReleased,hostile),width,height);
  require(command.orders.size()==2&&command.orders[0].formation_id==11&&
              command.orders[1].formation_id==12&&
              std::ranges::all_of(command.orders,[](const auto&order){
                return order.type==MassiveCombatOrderType::Engage&&
                    order.target_formation_id==std::optional<std::int64_t>{77};
              }),"context target did not command every selected owned formation");

  (void)workspace.handle(press(InputEventType::LeftPressed,
      center(layout.order_buttons[2])),width,height);
  (void)workspace.handle(press(InputEventType::LeftReleased,
      center(layout.order_buttons[2])),width,height);
  (void)workspace.handle(press(InputEventType::LeftPressed,hostile),width,height);
  command=workspace.handle(press(InputEventType::LeftReleased,hostile),width,height);
  require(command.orders.size()==1&&command.orders.front().formation_id==11&&
              command.orders.front().target_formation_id==
                  std::optional<std::int64_t>{77},
          "targeted order did not retain the first selected owned source only");
}

void speed_and_chrome() {
  constexpr int width = 1280, height = 720;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  workspace.set_tactical_speed(0.,1.);
  auto command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.play)), width, height);
  require(command.kind == BattleWorkspaceCommandKind::TogglePause,
          "play button must toggle pause");
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.speed)), width, height);
  require(command.kind == BattleWorkspaceCommandKind::CycleSpeed,
          "speed button must cycle");
  require(command.orders.empty()&&command.speed==0.,
          "paused speed selection tried to mutate simulation state directly");
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.menu)), width, height);
  require(command.kind == BattleWorkspaceCommandKind::Menu,
          "menu button missing");
  command = workspace.handle(press(InputEventType::EscapePressed, {}), width,
                             height);
  require(command.kind == BattleWorkspaceCommandKind::Menu,
          "escape must open the menu");
}

void ship_art_hit_targets() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  const BattleShipTarget first{11, {700, 400}, 120, 45};
  const BattleShipTarget second{12, {850, 450}, 100, -20};
  workspace.set_ship_targets({first, second}, width, height);

  const auto radians = first.heading_degrees * .01745329251994329577f;
  const Point hull{first.center.x + std::cos(radians) * first.size * .3f,
                   first.center.y + std::sin(radians) * first.size * .3f};
  (void)workspace.handle(press(InputEventType::LeftPressed, hull), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, hull), width,
                         height);
  require(workspace.selection() == std::set<std::int64_t>{11},
          "rotated visible ship hull did not select its formation");

  workspace.set_ship_targets(
      {{11, {780, 420}, 120, 0}, {12, {780, 420}, 120, 0}}, width,
      height);
  (void)workspace.handle(press(InputEventType::LeftPressed, {780, 420}), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, {780, 420}), width,
                         height);
  require(workspace.selection() == std::set<std::int64_t>{12},
          "overlapping ship hits did not prefer last-drawn geometry");
  workspace.set_ship_targets({first, second}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, hull), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, hull), width,
                         height);

  (void)workspace.handle(press(InputEventType::RightPressed, second.center),
                         width, height);
  const auto command = workspace.handle(
      press(InputEventType::RightReleased, second.center), width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder &&
              command.orders.size() == 1 &&
              command.orders.front().formation_id == 11 &&
              command.orders.front().target_formation_id ==
                  std::optional<std::int64_t>{12},
          "ship context hit changed the authoritative formation identities");

  workspace.set_ship_targets({first}, width, height);
  const Point transparent_corner{first.center.x + first.size * .4f,
                                 first.center.y + first.size * .4f};
  (void)workspace.handle(
      press(InputEventType::LeftPressed, transparent_corner), width, height);
  (void)workspace.handle(
      press(InputEventType::LeftReleased, transparent_corner), width, height);
  require(workspace.selection().empty(),
          "transparent artwork corner selected the formation");
  const Point plume{first.center.x - std::cos(radians) * first.size * .65f,
                    first.center.y - std::sin(radians) * first.size * .65f};
  (void)workspace.handle(press(InputEventType::LeftPressed, plume), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, plume), width,
                         height);
  require(workspace.selection().empty(),
          "transparent thruster plume selected the formation");

  workspace.set_ship_targets({BattleShipTarget{77, {760, 470}, 140, 0}},
                             width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, {760, 470}), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, {760, 470}), width,
                         height);
  require(workspace.selection().empty(),
          "foreign artwork exposed owned controls");

  std::vector<BattleShipTarget> capped;
  for (int index = 0; index < 32; ++index)
    capped.push_back({11, {100.f + index * 14.f, 520}, 10, 0});
  capped.push_back({11, {900, 520}, 80, 0});
  workspace.set_ship_targets(std::move(capped), width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, {900, 520}), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, {900, 520}), width,
                         height);
  require(workspace.selection().empty(),
          "ship target storage exceeded its hard limit");

  workspace.set_ship_targets({first}, width, height);
  auto refreshed = snapshot();
  ++refreshed.tick;
  workspace.set_snapshot(refreshed, .1);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().contains(11),
          "same-battle observer refresh discarded last-drawn ship geometry");

  workspace.set_ship_targets({first}, width, height);
  auto changed_battle = snapshot();
  changed_battle.battle_id[0] = 99;
  workspace.set_snapshot(changed_battle, .1);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().empty(),
          "changed battle retained stale ship geometry");

  workspace.set_snapshot(snapshot(), .1);
  workspace.set_ship_targets({first}, width, height);
  auto departed = snapshot();
  std::erase_if(departed.formations, [](const MassiveObservedFormation &value) {
    return value.formation_id == 11;
  });
  workspace.set_snapshot(departed, .1);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().empty(),
          "departed formation retained stale ship geometry");
  workspace.set_snapshot(snapshot(), .1);

  workspace.set_ship_targets({first}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, {690, 390}), width,
                         height);
  InputEvent drag = press(InputEventType::PointerMove, {710, 410});
  drag.delta = {20, 20};
  (void)workspace.handle(drag, width, height);
  (void)workspace.handle(press(InputEventType::LeftReleased, {710, 410}), width,
                         height);
  require(workspace.selection().contains(11),
          "box selection omitted the visible owned hull center");

  workspace.set_ship_targets({first}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), 1920,
                         1080);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         1920, 1080);
  require(workspace.selection().empty(),
          "resized viewport retained stale ship geometry");
  const BattleShipTarget resized{11, {1000, 600}, 120, 0};
  DrawList resized_draw;
  workspace.render(
      resized_draw, 1920, 1080,
      [&](DrawList &, const UiRect &, float, float) {
        workspace.set_ship_targets({resized}, 1920, 1080);
      });
  (void)workspace.handle(press(InputEventType::LeftPressed, resized.center),
                         1920, 1080);
  (void)workspace.handle(press(InputEventType::LeftReleased, resized.center),
                         1920, 1080);
  require(workspace.selection().contains(11),
          "fresh resized draw did not restore ship interaction");

  workspace.set_ship_targets({first}, width, height);
  InputEvent wheel = press(InputEventType::Wheel, {600, 350});
  wheel.wheel_y = 1;
  (void)workspace.handle(wheel, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().empty(),
          "camera mutation retained stale ship geometry");
  workspace.set_ship_targets({first}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().contains(11),
          "fresh ship geometry was not restored after camera redraw");

  workspace.set_ship_targets({first}, width, height);
  const auto fit_button =
      center(BattleWorkspaceLayout::for_viewport(width, height).fit);
  (void)workspace.handle(press(InputEventType::LeftPressed, fit_button), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, fit_button), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().empty(),
          "FIT retained stale ship geometry");

  const BattleShipTarget pan_target{11, {900, 560}, 80, 0};
  workspace.set_ship_targets({pan_target}, width, height);
  (void)workspace.handle(press(InputEventType::RightPressed, {500, 300}), width,
                         height);
  InputEvent pan = press(InputEventType::PointerMove, {525, 315});
  pan.delta = {25, 15};
  (void)workspace.handle(pan, width, height);
  (void)workspace.handle(press(InputEventType::RightReleased, {525, 315}), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftPressed, pan_target.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, pan_target.center),
                         width, height);
  require(workspace.selection().empty(),
          "camera pan retained stale ship geometry");

  const auto play = center(BattleWorkspaceLayout::for_viewport(width, height).play);
  workspace.set_ship_targets({BattleShipTarget{11, play, 200, 0}}, width,
                             height);
  const auto chrome = workspace.handle(
      press(InputEventType::LeftPressed, play), width, height);
  require(chrome.kind == BattleWorkspaceCommandKind::TogglePause &&
              workspace.selection().empty(),
          "ship target bypassed authoritative chrome routing");

  const auto formation_center = workspace.project({-60, 0}, width, height);
  const BattleShipTarget label_blocker{
      11, {formation_center.x + 60, formation_center.y}, 100, 0};
  DrawList draw;
  workspace.render(
      draw, width, height,
      [&](DrawList &, const UiRect &, float, float) {
        workspace.set_ship_targets({label_blocker}, width, height);
      });
  const auto label = std::ranges::find_if(draw.overlay, [](const auto &item) {
    const auto text = std::get_if<Text>(&item);
    return text && text->value == "Vanguard Fleet";
  });
  require(label != draw.overlay.end(), "owned formation label disappeared");
  const auto *label_text = std::get_if<Text>(&*label);
  require(label_text && label_text->clip, "formation label has no bounds");
  const UiRect ship_bounds{label_blocker.center.x - 50,
                           label_blocker.center.y - 50, 100, 100};
  const auto &label_bounds = *label_text->clip;
  require(label_bounds.x + label_bounds.width <= ship_bounds.x ||
              ship_bounds.x + ship_bounds.width <= label_bounds.x ||
              label_bounds.y + label_bounds.height <= ship_bounds.y ||
              ship_bounds.y + ship_bounds.height <= label_bounds.y,
          "formation label overlapped current ship artwork");

  workspace.set_ship_targets({first}, width, height);
  workspace.close();
  workspace.open(snapshot(), 1, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, first.center), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, first.center),
                         width, height);
  require(workspace.selection().empty(),
          "closed workspace retained stale ship geometry");
}

void camera_roundtrip_and_resize() {
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(),1,1280,720);
  const auto own=workspace.project({-60.f,0.f},1280,720);
  (void)workspace.handle(press(InputEventType::LeftPressed,own),1280,720);
  (void)workspace.handle(press(InputEventType::LeftReleased,own),1280,720);
  const auto field=Point{500.f,350.f};
  (void)workspace.handle(press(InputEventType::RightPressed,field),1280,720);
  InputEvent pan{};pan.type=InputEventType::PointerMove;
  pan.position={-180.f,-90.f};pan.delta={-680.f,-440.f};
  (void)workspace.handle(pan,1280,720);
  (void)workspace.handle(press(InputEventType::RightReleased,pan.position),1280,720);
  for(const auto [width,height]:{std::pair{1280,720},std::pair{1920,1080},
                                std::pair{3840,2160}}){
    const MassivePoint expected{13.f,-27.f};
    const auto screen=workspace.project(expected,width,height);
    (void)workspace.handle(press(InputEventType::RightPressed,screen),width,height);
    const auto command=workspace.handle(press(InputEventType::RightReleased,screen),width,height);
    require(command.orders.size()==1&&command.orders.front().objective&&
                std::abs(command.orders.front().objective->x-expected.x)<.01f&&
                std::abs(command.orders.front().objective->y-expected.y)<.01f,
            "camera world/screen round trip failed after zero, negative, or resized center");
  }
}

void malformed_geometry_is_bounded() {
  auto view=snapshot();
  view.formations.push_back(formation(90,1,
      {std::numeric_limits<float>::quiet_NaN(),0.f},"Invalid"));
  view.formations.push_back(formation(91,9,{1.e30f,-1.e30f},"Huge"));
  view.formations.front().velocity={200000.f,-200000.f};
  NativeBattleWorkspace workspace;
  workspace.open(std::move(view),1,1280,720);
  const auto own=workspace.project({-60.f,0.f},1280,720);
  (void)workspace.handle(press(InputEventType::LeftPressed,own),1280,720);
  (void)workspace.handle(press(InputEventType::LeftReleased,own),1280,720);
  InputEvent invalid{};invalid.type=InputEventType::PointerMove;
  invalid.position={std::numeric_limits<float>::infinity(),0.f};
  invalid.delta={std::numeric_limits<float>::infinity(),0.f};
  const auto rejected=workspace.handle(invalid,1280,720);
  require(!rejected.captured,"nonfinite pointer input was accepted");
  for(int i=0;i<32;++i){InputEvent wheel{};wheel.type=InputEventType::Wheel;
    wheel.position={640.f,360.f};wheel.wheel_y=1.f;
    (void)workspace.handle(wheel,1280,720);}
  DrawList draw;workspace.render(draw,1280,720);
  require(draw.overlay.size()<5000&&workspace.rendered_tokens()<=4096,
          "huge geometry exceeded the bounded presentation budget");
  for(const auto&item:draw.overlay){
    if(const auto*line=std::get_if<Line>(&item))
      require(std::isfinite(line->from.x)&&std::isfinite(line->from.y)&&
                  std::isfinite(line->to.x)&&std::isfinite(line->to.y),
              "malformed geometry emitted a nonfinite line");
    if(const auto*mesh=std::get_if<TriangleMesh>(&item))
      require(std::ranges::all_of(mesh->vertices,[](Point point){
        return std::isfinite(point.x)&&std::isfinite(point.y);
      }),"malformed geometry emitted a nonfinite mesh");
  }
  auto invalid_only=snapshot();
  invalid_only.formations.clear();
  invalid_only.formations.push_back(formation(99,1,
      {std::numeric_limits<float>::quiet_NaN(),1.e30f},"Invalid only"));
  NativeBattleWorkspace invalid_workspace;
  invalid_workspace.open(std::move(invalid_only),1,1280,720);
  const auto origin=invalid_workspace.project({},1280,720);
  require(std::isfinite(origin.x)&&std::isfinite(origin.y),
          "invalid-only encounter left the camera uninitialized");
}

void snapshot_drops_departed_selection() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  const auto own = workspace.project({-60.f, 0.f}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  require(workspace.selected_count() == 1, "selection not established");
  auto next = snapshot();
  next.formations.erase(next.formations.begin());
  workspace.set_snapshot(std::move(next), .1);
  require(workspace.selection().empty(),
          "departed formation must leave the selection");
}

void observer_secrecy_rendering() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  auto view = snapshot();
  view.formations[2].ship_count_low = 80;
  view.formations[2].ship_count_high = 160;
  view.formations[2].is_exact = false;
  workspace.open(view, 1, width, height);
  DrawList draw;
  workspace.render(draw, width, height);
  const auto tokens = workspace.rendered_tokens();
  require(tokens > 0, "tokens missing");

  // The unidentified formation contributes only range-midpoint tokens; with a
  // 120-ship midpoint at fit zoom the per-formation cap stays below 28+lead.
  workspace.close();
  require(!workspace.visible() && workspace.snapshot() == nullptr,
          "close must release the snapshot");
  workspace.discard_campaign();
}

void targeted_chrome_gesture_does_not_order() {
  NativeBattleWorkspace workspace;
  constexpr int width=1280, height=720;
  workspace.open(snapshot(),1,width,height);
  const auto own=workspace.project({-60.f,0.f},width,height);
  const auto enemy=workspace.project({60.f,10.f},width,height);
  const auto layout=BattleWorkspaceLayout::for_viewport(width,height);
  (void)workspace.handle(press(InputEventType::LeftPressed,own),width,height);
  (void)workspace.handle(press(InputEventType::LeftReleased,own),width,height);
  const auto focus=std::ranges::find_if(battle_order_buttons(),[](const auto& value){
    return value.type==MassiveCombatOrderType::FocusFire;
  });
  const auto index=static_cast<std::size_t>(focus-battle_order_buttons().begin());
  (void)workspace.handle(press(InputEventType::LeftPressed,center(layout.order_buttons[index])),width,height);
  auto command=workspace.handle(press(InputEventType::LeftReleased,enemy),width,height);
  require(command.orders.empty(),"Targeted button press released over field issued an order.");
  (void)workspace.handle(press(InputEventType::LeftPressed,center(layout.event_feed)),width,height);
  command=workspace.handle(press(InputEventType::LeftReleased,enemy),width,height);
  require(command.orders.empty(),"Report feed press escaped into a targeted order.");
  (void)workspace.handle(press(InputEventType::LeftPressed,enemy),width,height);
  command=workspace.handle(press(InputEventType::LeftReleased,enemy),width,height);
  require(command.orders.size()==1&&command.orders.front().target_formation_id==77,
          "Fresh field click did not issue the intended targeted order.");
}

void spatial_orders_preserve_depth() {
  constexpr int width=1280,height=720;
  auto view=snapshot();view.formations[0].position.z=120.f;
  NativeBattleWorkspace workspace;workspace.open(view,1,width,height);
  const auto own=workspace.project(view.formations[0].position,width,height);
  const auto select=[&]{
    (void)workspace.handle(press(InputEventType::LeftPressed,own),width,height);
    (void)workspace.handle(press(InputEventType::LeftReleased,own),width,height);
  };
  select();
  auto wheel=press(InputEventType::Wheel,own);wheel.alt=true;wheel.wheel_y=1.f;
  auto command=workspace.handle(wheel,width,height);
  require(command.kind==BattleWorkspaceCommandKind::IssueOrder&&command.orders.size()==1&&
      command.orders.front().formation_id==11&&command.orders.front().objective&&
      command.orders.front().objective->z==220.f&&
      command.orders.front().objective->x==-60.f&&command.orders.front().objective->y==0.f,
      "Depth control must issue an authoritative 3D movement order.");
  require(workspace.project(view.formations[0].position,width,height).x==own.x&&
      workspace.project(view.formations[0].position,width,height).y==own.y,
      "Depth order must not zoom the camera or mutate the observed snapshot.");
  wheel.wheel_y=-1.f;command=workspace.handle(wheel,width,height);
  require(command.orders.size()==1&&command.orders.front().objective->z==20.f,
      "Depth control did not support descending.");
  const MassivePoint destination{20.f,-30.f,120.f};
  const auto at=workspace.project(destination,width,height);
  (void)workspace.handle(press(InputEventType::RightPressed,at),width,height);
  command=workspace.handle(press(InputEventType::RightReleased,at),width,height);
  require(command.orders.size()==1&&command.orders.front().objective&&
      std::abs(command.orders.front().objective->x-destination.x)<.01f&&
      std::abs(command.orders.front().objective->y-destination.y)<.01f&&
      command.orders.front().objective->z==destination.z,
      "A field movement order must preserve formation depth and unproject correctly.");
  wheel.position=center(BattleWorkspaceLayout::for_viewport(width,height).orders);
  require(workspace.handle(wheel,width,height).orders.empty(),
      "Depth control leaked through the orders panel.");
}

void fit_keeps_formations_clear_of_controls() {
  for(const auto [width,height]:{std::pair{1280,720},{1920,1080},{3840,2160}}){
    auto view=snapshot();
    view.formations[0].position={-420.f,-120.f};
    view.formations[1].position={100.f,0.f};
    view.formations[2].position={420.f,120.f};
    NativeBattleWorkspace workspace;
    workspace.open(view,1,width,height);
    const auto layout=BattleWorkspaceLayout::for_viewport(width,height);
    for(const auto& formation:view.formations){
      const auto point=workspace.project(formation.position,width,height);
      require(!layout.event_feed.contains(point)&&!layout.orders.contains(point)&&
                  !layout.top_row.contains(point)&&!layout.battle_summary.contains(point),
              "Fit placed a formation beneath the event feed or controls.");
    }
  }
}

void keyboard_focus() {
  constexpr std::uint32_t kTab = 9u;
  constexpr std::uint32_t kReturn = 13u;
  constexpr std::uint32_t kHome = 0x4000004au;
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  const auto key = [&](std::uint32_t k, bool shift = false) {
    InputEvent e{InputEventType::KeyPressed};
    e.key = k;
    e.shift = shift;
    return workspace.handle(e, width, height);
  };
  require(workspace.focus() < 0, "Battle focus should start unset.");
  const auto ring_layout = BattleWorkspaceLayout::for_viewport(width, height);
  require(workspace.focused_label(ring_layout).empty(),
          "Unfocused battle workspace reported a label.");
  // Top row: play, speed, fit, menu; then the two-column order grid.
  require(key(kTab).captured && workspace.focus() == 0,
          "Tab did not land on the play control.");
  require(workspace.focused_label(ring_layout) == "Pause",
          "Focused play control label mismatch.");
  auto command = key(kReturn);
  require(command.kind == BattleWorkspaceCommandKind::TogglePause &&
              workspace.focus() == 0,
          "Play activation did not toggle pause.");
  (void)key(kTab);
  command = key(kReturn);
  require(command.kind == BattleWorkspaceCommandKind::CycleSpeed &&
              workspace.focus() == 1,
          "Speed activation did not cycle speed.");
  (void)key(kTab);
  (void)key(kTab);
  command = key(kReturn);
  require(command.kind == BattleWorkspaceCommandKind::Menu,
          "Menu activation did not issue the menu command.");
  require(workspace.visible(), "Menu command closed the battle surface.");
  // Order buttons activate through the same press dispatch.
  const int hold = 4; // first order-grid entry
  (void)key(kHome);
  for (int i = 0; i < hold; ++i)
    (void)key(kTab);
  require(workspace.focus() == hold,
          "Hold order is not the first order-grid focusable.");
  require(workspace.focused_label(ring_layout) == "Hold",
          "Focused order button label mismatch.");
  command = key(kReturn);
  require(command.captured && command.kind == BattleWorkspaceCommandKind::None,
          "Order issued without a selection.");
  // Select an owned formation, then keyboard-activate Hold.
  const auto own = workspace.project({-60.f, 0.f}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                       height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  require(workspace.selection().contains(11), "selection failed");
  require(workspace.focus() < 0,
          "Pointer selection did not clear keyboard focus.");
  for (int i = 0; i <= hold; ++i)
    (void)key(kTab);
  require(workspace.focus() == hold,
          "Refocus did not reach the Hold order.");
  command = key(kReturn);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder &&
              command.orders.size() == 1 &&
              command.orders.front().formation_id == 11 &&
              command.orders.front().type == MassiveCombatOrderType::Hold,
          "Keyboard activation did not issue the Hold order.");
  require(workspace.focus() == hold,
          "Order activation lost keyboard focus.");
  // Pointer cancellation clears focus.
  (void)workspace.handle(press(InputEventType::PointerCancelled, {}), width,
                         height);
  require(workspace.focus() < 0,
          "Pointer cancellation did not clear keyboard focus.");
}

} // namespace

int main() {
  try {
    responsive_layout();
    open_fit_and_render();
    selection_rules();
    gesture_ownership();
    order_commands();
    keyboard_focus();
    speed_and_chrome();
    ship_art_hit_targets();
    camera_roundtrip_and_resize();
    malformed_geometry_is_bounded();
    snapshot_drops_departed_selection();
    observer_secrecy_rendering();
    targeted_chrome_gesture_does_not_order();
    spatial_orders_preserve_depth();
    fit_keeps_formations_clear_of_controls();
  } catch (const std::exception &error) {
    std::cerr << "native battle workspace tests failed: " << error.what()
              << '\n';
    return 1;
  }
  std::cout << "native battle workspace tests passed\n";
  return 0;
}
