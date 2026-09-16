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

NativeSurfaceBuildOption build_option(std::string type_id, std::string name,
                                      double power_supply = 0.,
                                      double power_demand = 0.) {
  return {.type_id = std::move(type_id), .name = std::move(name),
          .industry_cost = 40., .authorization_budget_units = 50.,
          .formatted_authorization = "$500M UED", .power_supply = power_supply,
          .power_demand = power_demand, .workforce_required_millions = .02};
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
  result.required_habitat_systems = 1;
  result.specialization_name = "Balanced settlement";
  result.specialization_description = "No dominant surface specialization";
  result.construction_sites = {site(1, true), site(2)};
  return result;
}

void responsive_layout_is_contained_and_nonoverlapping() {
  for (const auto [width, height] :
       {std::pair{1280, 720}, std::pair{1920, 1080},
        std::pair{2560, 1440}, std::pair{3840, 2160}}) {
    const auto layout = ColonyWorkspaceLayout::for_viewport(width, height);
    const auto navigation = NativeUiLayout::for_viewport(width, height);
    const UiRect viewport{0, 0, static_cast<float>(width),
                          static_cast<float>(height)};
    REQUIRE(contains(viewport, layout.surface));
    REQUIRE(layout.surface.x >=
            navigation.research.x + navigation.research.width);
    for (const auto bounds : {layout.details, layout.summary,
                              layout.sustenance, layout.sites,
                              layout.site_rows, layout.close})
      REQUIRE(contains(layout.surface, bounds));
    REQUIRE(!overlaps(layout.summary, layout.sustenance));
    REQUIRE(!overlaps(layout.sustenance, layout.operations));
    REQUIRE(!overlaps(layout.operations, layout.sites));
    REQUIRE(!overlaps(layout.summary, layout.sites));
  }
}

void rendering_uses_canonical_sections_and_clipped_progress() {
  NativeColonyWorkspace workspace;
  workspace.open(view());
  DrawList draw;
  workspace.render(draw, 1280, 720);
  const auto layout = ColonyWorkspaceLayout::for_viewport(1280, 720);
  bool support{}, projected{}, local_currency{}, power{}, modules{}, progress{};
  for (const auto &command : draw.overlay) {
    if (const auto *label = std::get_if<Text>(&command)) {
      support |= label->value == "SUPPORT & LABOR";
      projected |= label->value.find("Projected surface production") !=
                   std::string::npos;
      local_currency |= label->value.find("+$1.3M UED/day") !=
                        std::string::npos;
      power |= label->value.find("Power 12.0 supplied / 8.0 required") !=
               std::string::npos;
      modules |= label->value == "SURFACE MODULES & CONSTRUCTION";
      if (label->value.find("Power Generator") != std::string::npos)
        REQUIRE(label->clip && contains(layout.site_rows, *label->clip));
    }
    if (const auto *rectangle = std::get_if<FilledRectangle>(&command)) {
      REQUIRE(std::isfinite(rectangle->bounds.x));
      REQUIRE(std::isfinite(rectangle->bounds.y));
      REQUIRE(std::isfinite(rectangle->bounds.width));
      REQUIRE(std::isfinite(rectangle->bounds.height));
      if (rectangle->bounds.height <= 5.01f * layout.scale &&
          contains(layout.site_rows, rectangle->bounds))
        progress = true;
    }
  }
  REQUIRE(support && projected && local_currency && power && modules && progress);
}

void detail_scroll_reaches_outpost_rows_and_keeps_text_clipped() {
  auto content = view();
  content.resource_outpost = true;
  content.population_millions = .042;
  content.outpost_status = "Confirmed extraction site";
  content.deposit_material_name = "Helium-3";
  content.deposit_grade = "Rich";
  content.has_confirmed_deposit = true;
  content.extraction_per_day = 2.5;
  content.stored_extracted_materials = 8.0;
  content.extracted_material_capacity = 20.0;
  content.cargo_transfer_capacity_per_day = 1.5;
  NativeColonyWorkspace workspace;
  workspace.open(std::move(content));
  const auto layout = ColonyWorkspaceLayout::for_viewport(1280, 720);
  REQUIRE(workspace.handle({.type = InputEventType::Wheel,
                            .position = center(layout.details),
                            .wheel_y = -100.f},
                           1280, 720).captured);
  DrawList draw;
  workspace.render(draw, 1280, 720);
  bool population{}, cargo{};
  for (const auto &item : draw.overlay) {
    const auto *label = std::get_if<Text>(&item);
    if (!label) continue;
    population |= label->value.find("42.0K") != std::string::npos;
    cargo |= label->value.find("Cargo transfer 1.5/day") != std::string::npos;
    if (label->clip && overlaps(*label->clip, layout.details))
      REQUIRE(contains(layout.details, *label->clip));
  }
  REQUIRE(cargo);
  // The compact personnel value is verified before scrolling the summary away.
  NativeColonyWorkspace fresh;
  auto compact = view();
  compact.population_millions = .042;
  fresh.open(std::move(compact));
  DrawList initial;
  fresh.render(initial, 1280, 720);
  population = std::ranges::any_of(initial.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value.find("42.0K") != std::string::npos;
  });
  REQUIRE(population);
}

void wheel_reaches_last_site_and_escape_closes() {
  auto content = view();
  content.construction_sites.clear();
  for (int id = 1; id <= 20; ++id)
    content.construction_sites.push_back(site(id));
  NativeColonyWorkspace workspace;
  workspace.open(std::move(content));
  const auto layout = ColonyWorkspaceLayout::for_viewport(1280, 720);
  const auto command = workspace.handle(
      {.type = InputEventType::Wheel,
       .position = center(layout.site_rows),
       .wheel_y = -100.f},
      1280, 720);
  REQUIRE(command.captured);
  DrawList draw;
  workspace.render(draw, 1280, 720);
  REQUIRE(std::ranges::any_of(draw.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value.ends_with("20");
  }));
  auto reduced = view();
  reduced.construction_sites = {site(1)};
  workspace.set_view(std::move(reduced));
  DrawList reduced_draw;
  workspace.render(reduced_draw, 1280, 720);
  REQUIRE(std::ranges::any_of(reduced_draw.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value.ends_with("1");
  }));
  const auto closed = workspace.handle({InputEventType::EscapePressed},
                                       1280, 720);
  REQUIRE(closed.kind == ColonyWorkspaceCommandKind::Close &&
          closed.captured && !workspace.visible());
}

void freight_review_input_and_layout() {
  for (const auto size : {Point{1280,720},Point{1920,1080},Point{3840,2160}}) {
    const auto width=static_cast<int>(size.x),height=static_cast<int>(size.y);
    const auto layout=ColonyWorkspaceLayout::for_viewport(width,height,true);
    REQUIRE(contains(layout.surface,layout.collect_freight));
    REQUIRE(!overlaps(layout.collect_freight,layout.open_surface));
    REQUIRE(!overlaps(layout.title,layout.collect_freight));
    REQUIRE(contains(layout.surface,layout.freight_review));
    REQUIRE(contains(layout.freight_review,layout.freight_text));
    REQUIRE(contains(layout.freight_review,layout.freight_confirm));
    REQUIRE(!overlaps(layout.freight_text,layout.freight_confirm));
    REQUIRE(!overlaps(layout.freight_cancel,layout.freight_confirm));
    REQUIRE(!overlaps(layout.freight_notice,layout.site_rows));
    NativeColonyWorkspace workspace;auto owned=view();owned.resource_outpost=true;
    workspace.open(owned);
    const auto click=[&](UiRect bounds){const auto point=center(bounds);(void)workspace.handle({InputEventType::LeftPressed,point},width,height);return workspace.handle({InputEventType::LeftReleased,point},width,height);};
    REQUIRE(workspace.handle({InputEventType::LeftReleased,center(layout.collect_freight)},width,height).kind==ColonyWorkspaceCommandKind::None);
    REQUIRE(click(layout.collect_freight).kind==ColonyWorkspaceCommandKind::ReviewFreight);
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

void campaign_replacement_clears_owned_snapshot() {
  NativeColonyWorkspace workspace;
  workspace.open(view(7));
  workspace.discard_campaign();
  REQUIRE(!workspace.visible() && !workspace.view());
}

void empty_colony_guidance_uses_live_options_and_resolves_after_construction() {
  auto empty = view();
  empty.construction_sites.clear();
  empty.power_supply = 1.;
  empty.power_demand = 3.;
  empty.available_buildings = {build_option("fabricator", "Fabricator", 0., 2.),
                               build_option("power_generator", "Power Generator", 4.)};
  NativeColonyWorkspace workspace;
  workspace.open(empty);
  DrawList draw;
  workspace.render(draw, 1280, 720);
  const auto has = [&draw](std::string_view value) {
    return std::ranges::any_of(draw.overlay, [value](const auto &item) {
      const auto *label = std::get_if<Text>(&item);
      return label && label->value.find(value) != std::string::npos;
    });
  };
  REQUIRE(has("DEVELOP THIS COLONY"));
  REQUIRE(has("Available next: Power Generator"));
  REQUIRE(has("$500M UED authorization"));
  REQUIRE(has("incoming materials over time"));
  empty.construction_sites = {site(1)};
  workspace.set_view(empty);
  DrawList resolved;
  workspace.render(resolved, 1280, 720);
  REQUIRE(std::ranges::none_of(resolved.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value == "DEVELOP THIS COLONY";
  }));

  empty.construction_sites.clear();
  empty.available_buildings.clear();
  workspace.set_view(empty);
  DrawList unavailable;
  workspace.render(unavailable, 1280, 720);
  REQUIRE(std::ranges::any_of(unavailable.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value.find("No eligible surface modules") != std::string::npos;
  }));

  // Authorization is credit-based: a newly founded colony can begin a
  // fabricator while its construction materials accumulate over time.
  empty.available_buildings = {build_option("fabricator", "Fabricator", 0., 2.)};
  empty.power_supply = 6.;
  empty.power_demand = 3.;
  empty.stored_industry = 1.;
  empty.treasury_budget_units = 120.;
  workspace.set_view(empty);
  DrawList low_stockpile;
  workspace.render(low_stockpile, 1280, 720);
  REQUIRE(std::ranges::any_of(low_stockpile.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value == "Available next: Fabricator";
  }));

  empty.treasury_budget_units = 10.;
  workspace.set_view(empty);
  DrawList no_funds;
  workspace.render(no_funds, 1280, 720);
  REQUIRE(std::ranges::any_of(no_funds.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value.find("More treasury is needed") != std::string::npos;
  }));
}
} // namespace

int main() {
  try {
    responsive_layout_is_contained_and_nonoverlapping();
    rendering_uses_canonical_sections_and_clipped_progress();
    detail_scroll_reaches_outpost_rows_and_keeps_text_clipped();
    wheel_reaches_last_site_and_escape_closes();
    campaign_replacement_clears_owned_snapshot();
    empty_colony_guidance_uses_live_options_and_resolves_after_construction();
    freight_review_input_and_layout();
    std::cout << "native colony workspace tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
