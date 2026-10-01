#include <stellar/core/galaxy_projection.hpp>

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/lane_network.hpp>

namespace stellar::core {
namespace {

std::string class_label(StellarClass value) {
    switch (value) {
    case StellarClass::MRedDwarf: return "M red dwarf";
    case StellarClass::KOrangeDwarf: return "K orange dwarf";
    case StellarClass::GYellowDwarf: return "G yellow dwarf";
    case StellarClass::FYellowWhiteDwarf: return "F yellow-white dwarf";
    case StellarClass::AWhiteStar: return "A white star";
    case StellarClass::HotBlueStar: return "hot blue star";
    case StellarClass::Giant: return "giant";
    case StellarClass::WhiteDwarf: return "white dwarf";
    case StellarClass::NeutronStar: return "neutron star";
    case StellarClass::BlackHole: return "black hole";
    case StellarClass::Protostar: return "protostar";
    case StellarClass::Pulsar: return "pulsar";
    }
    return {};
}

std::string system_label(const StellarSystem &system) {
    std::string label;
    if (system.primary)
        label = class_label(*system.primary);
    if (system.secondary) {
        if (!label.empty())
            label += " + ";
        label += class_label(*system.secondary);
    }
    return label;
}

} // namespace

engine::GalaxyMap project_galaxy_map(const FreshCampaignState &state) {
    engine::GalaxyMap map;
    for (const auto &system : state.systems) {
        engine::GalaxySystem row;
        row.id = static_cast<std::uint64_t>(system.id);
        row.name = system.name;
        row.x_light_years = system.position.x;
        row.y_light_years = system.position.y;
        row.depth_light_years = system.position.depth_light_years.value_or(0.0);
        row.classification = system_label(system);
        if (system.has_habitable_world)
            row.tags.push_back("habitable");
        if (system.has_anomaly)
            row.tags.push_back("anomaly");
        if (system.has_rare_resource)
            row.tags.push_back("rare-resource");
        if (system.has_pre_warp_civilization)
            row.tags.push_back("pre-warp");
        map.add_system(std::move(row));
    }

    InterstellarLaneNetwork lanes(&state.systems);
    std::uint64_t lane_id = 1;
    for (const auto &lane : lanes.build()) {
        map.add_lane(engine::GalaxyLane{lane_id++,
                                        static_cast<std::uint64_t>(
                                            lane.first_system_id),
                                        static_cast<std::uint64_t>(
                                            lane.second_system_id),
                                        lane.length_light_years, true});
    }

    std::uint64_t marker_id = 1;
    for (const auto &colony : state.colonies) {
        const auto *system = map.system(
            static_cast<std::uint64_t>(colony.system_id));
        if (!system)
            continue; // colony on a removed/legacy system — skip marker
        engine::GalaxyMarker marker;
        marker.id = marker_id++;
        marker.kind = engine::GalaxyMarker::Kind::Colony;
        marker.label = colony.name;
        marker.owner_id = static_cast<std::uint64_t>(colony.civilization_id);
        marker.x_light_years = system->x_light_years;
        marker.y_light_years = system->y_light_years;
        marker.depth_light_years = system->depth_light_years;
        marker.system_id = static_cast<std::uint64_t>(colony.system_id);
        map.add_marker(std::move(marker));
    }

    for (const auto &fleet : state.fleets) {
        engine::GalaxyMarker marker;
        marker.id = marker_id++;
        marker.kind = engine::GalaxyMarker::Kind::Fleet;
        marker.label = fleet.name.empty()
                           ? "Fleet " + std::to_string(fleet.id)
                           : fleet.name;
        marker.owner_id =
            static_cast<std::uint64_t>(fleet.civilization_id);
        if (fleet.current_system_id) {
            const auto *system = map.system(static_cast<std::uint64_t>(
                *fleet.current_system_id));
            if (system) {
                marker.system_id = system->id;
                marker.x_light_years = system->x_light_years;
                marker.y_light_years = system->y_light_years;
                marker.depth_light_years = system->depth_light_years;
            }
        }
        if (!marker.system_id) {
            marker.x_light_years = fleet.position.x;
            marker.y_light_years = fleet.position.y;
        }
        if (fleet.destination_system_id)
            marker.destination_system_id = static_cast<std::uint64_t>(
                *fleet.destination_system_id);
        map.add_marker(std::move(marker));
    }
    return map;
}

} // namespace stellar::core
