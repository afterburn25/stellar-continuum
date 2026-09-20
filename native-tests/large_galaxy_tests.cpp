#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/engine/sha256.hpp>
#include <stellar/engine/spatial_region_index.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <set>
#include <limits>

using namespace stellar::core;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string hash(const std::string& text) {
  const auto bytes = stellar::engine::sha256({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
  std::string result;
  for (auto b : bytes) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; }
  return result;
}
void spatial_queries_match_full_scan() {
  using namespace stellar::engine;
  SpatialRegionIndex index(3.5);
  std::vector<SpatialBounds> bounds;
  for (int i=0;i<10000;++i) {
    const double x=(i%100-50)*7.,y=(i/100-50)*7.;
    bounds.push_back({x,y,x+(i%5)*3.5,y+(i%3)*3.5});
    index.insert(bounds.size()-1,bounds.back());
  }
  const auto compare=[&](SpatialBounds query) {
    std::vector<std::size_t> expected;
    for(std::size_t i=0;i<bounds.size();++i) {
      const auto& b=bounds[i];
      if(b.left<=query.right&&b.right>=query.left&&b.top<=query.bottom&&b.bottom>=query.top)expected.push_back(i);
    }
    check(index.query(query)==expected,"Adaptive spatial query differs from exact full scan");
  };
  for(int i=0;i<120;++i) {
    const double x=(i%20-10)*3.5,y=(i/20-3)*3.5;
    compare({x,y,x+3.5,y+3.5});
    compare({x,y,x,y});
  }
  compare({-1e300,-1e300,1e300,1e300});
  compare({500,500,800,800});
  bool rejected=false;
  try {(void)index.query({0,0,std::numeric_limits<double>::infinity(),1});}
  catch(const std::invalid_argument&) {rejected=true;}
  check(rejected,"Nonfinite spatial query accepted");
}
}
int main(int argc, char** argv) try {
  check(argc >= 2, "Catalog required");
  const auto catalog = load_nearby_catalog(argv[1]);
  const int count = argc > 2 ? std::stoi(argv[2]) : 2500;
  if(count==2500)spatial_queries_match_full_scan();
  GalaxyGenerationConfig config;
  config.base_seed = 8057; config.system_count = count;
  config.requested_population = PopulationSelection::Active;
  config = resolve_galaxy_configuration(config);
  const auto start = std::chrono::steady_clock::now();
  auto world = seed_persistable_fresh_campaign(config.base_seed, catalog,
      {"2050-03-21T00:00:00Z", count, 6, 1, "terran_baseline",
       StellarPopulationOptions{config.morphology, config.resolved_population}, false, config});
  const auto generated = std::chrono::steady_clock::now();
  check(world.systems.size()==static_cast<std::size_t>(count),"Requested galaxy size was truncated");
  std::set<std::string> generated_names;
  for(const auto& system:world.systems)if(!system.stellar_catalog_id)
    check(generated_names.insert(system.name).second,"Procedural system name repeated");
  const GalaxyPayloadCaptureOptions capture{0, "large-galaxy-test", "2050-03-21T00:00:00Z"};
  const auto payload = encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(world, capture));
  auto loaded = restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(payload)).galaxy;
  check(nlohmann::json::parse(payload) == nlohmann::json::parse(encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(loaded, capture))), "Large campaign save/load changed authoritative state");
  const auto persisted = std::chrono::steady_clock::now();
  InterstellarLaneNetwork network(world.systems);
  const auto lanes = network.build();
  const auto built = std::chrono::steady_clock::now();
  for (int origin : {0, count / 2, count - 1}) {
    const auto route = network.find_shortest_route(origin, (origin + count / 3) % count, 100000.);
    check(route.size() >= 2 && route.front() == origin, "Large campaign route is disconnected");
  }
  const auto routed = std::chrono::steady_clock::now();
  const auto digest=hash(nlohmann::json::parse(payload).dump());
  std::size_t fields=0;for(const auto& system:world.systems){check(system.small_body_fields.has_value(),"Missing deterministic small-body catalog");fields+=system.small_body_fields->size();}
  check(fields>0&&fields<static_cast<std::size_t>(count)*4,"Unbounded field generation");
  if(count==2500){
    // Preserve the pre-feature parity contract for every existing subsystem.
    // New versioned records are separately round-tripped in the full payload.
    auto legacy=world;for(auto& system:legacy.systems)system.small_body_fields.reset();for(auto& body:legacy.bodies)body.cracked_world=false;
    const auto legacy_payload=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(legacy,capture));
    check(hash(nlohmann::json::parse(legacy_payload).dump())=="9b9433d691ad9a1fa5583b3070fd76ecb8d3195285272aaa8d4caacccf233b8c",
      "Existing galaxy changed outside the small-body extension");
  }
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b-a).count(); };
  std::cout << "systems=" << count << " bodies=" << world.bodies.size() << " lanes=" << lanes.size()
            << " generation_ms=" << ms(start, generated) << " persistence_ms=" << ms(generated, persisted)
            << " lanes_ms=" << ms(persisted, built) << " routes_ms=" << ms(built, routed)
            << " fields=" << fields << " payload_bytes=" << payload.size() << " sha256=" << digest << '\n';
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
