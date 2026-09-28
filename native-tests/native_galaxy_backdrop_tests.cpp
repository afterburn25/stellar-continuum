#include "native_galaxy_backdrop.hpp"
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/galaxy_configuration.hpp>
#include <stellar/engine/asset_registry.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace stellar::native_galaxy_ui;
using namespace stellar::native_map;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

struct WorkerGate {
  struct State { std::mutex mutex; std::condition_variable changed; bool started{}, released{}; };
  std::shared_ptr<State> state{std::make_shared<State>()};
  ~WorkerGate() { release(); }
  static std::shared_ptr<const RgbaImage> block(const std::shared_ptr<State> &value) {
    std::unique_lock lock(value->mutex);
    value->started=true; value->changed.notify_all();
    value->changed.wait(lock,[&]{return value->released;});
    return RgbaImage::create(1,1,{0,0,0,0});
  }
  void wait_started() {
    std::unique_lock lock(state->mutex);
    require(state->changed.wait_for(lock,std::chrono::seconds(15),[&]{return state->started;}),
            "Galaxy worker did not reach the preparation barrier.");
  }
  void release() {
    { std::lock_guard lock(state->mutex); state->released=true; }
    state->changed.notify_all();
  }
};

class OversizeArtworkFixture final {
 public:
  explicit OversizeArtworkFixture(int kind) {
    try {
      parent_=std::filesystem::canonical(std::filesystem::temp_directory_path());
      const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
      root_=parent_/("stellar-galaxy-oversize-"+std::to_string(stamp));
      if(!std::filesystem::create_directory(root_))throw std::runtime_error("Galaxy fixture directory already exists.");
      owns_root_=true;
      std::filesystem::create_directories(root_/"assets/visual/space");
      const auto source=root_/"assets/visual/space"/(kind == 0 ? "deep-field-v3.png" : kind == 1 ? "star-background.png" : "regional-nebula-b.png");
      std::ofstream output(source,std::ios::binary|std::ios::trunc);
      if(!output)throw std::runtime_error("Could not create oversized galaxy fixture.");
      const auto put16=[&](std::uint16_t value){output.put(static_cast<char>(value&255u));output.put(static_cast<char>(value>>8u));};
      const auto put32=[&](std::uint32_t value){put16(static_cast<std::uint16_t>(value));put16(static_cast<std::uint16_t>(value>>16u));};
      const std::uint32_t width=kind<2?5121:2049,height=1024,bytes=width*height*4u;
      put16(0x4d42u);put32(54u+bytes);put16(0);put16(0);put32(54);put32(40);put32(width);put32(height);put16(1);put16(32);put32(0);put32(bytes);put32(0);put32(0);put32(0);put32(0);
      std::vector<std::uint8_t> row(static_cast<std::size_t>(width)*4u);
      for(std::uint32_t y{};y<height;++y)output.write(reinterpret_cast<const char*>(row.data()),static_cast<std::streamsize>(row.size()));
      if(!output)throw std::runtime_error("Could not write oversized galaxy fixture.");
    } catch (...) {cleanup();throw;}
  }
  ~OversizeArtworkFixture() {cleanup();}
  [[nodiscard]]const std::filesystem::path &root()const noexcept{return root_;}
 private:
  void cleanup() noexcept {
    if(!owns_root_)return;
    std::error_code error;
    const auto root=std::filesystem::weakly_canonical(root_,error);
    if(error||root.parent_path()!=parent_||root.filename().string().rfind("stellar-galaxy-oversize-",0)!=0)return;
    std::filesystem::remove_all(root,error);
  }
  std::filesystem::path parent_,root_;
  bool owns_root_{};
};

template<class Predicate> void await(Predicate ready) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
  while(!ready()) {
    require(std::chrono::steady_clock::now()<deadline,"Galaxy background preparation timed out.");
    std::this_thread::yield();
  }
}

GalaxyBackdropCatalog catalog(std::uint64_t generation = 7) {
  return {generation,
          -443,
          {{-120., -22.}, {-30., 74.}, {81., -65.}, {146., 18.}},
          WorldPoint{8., 3.},
          21.,
          false};
}

std::size_t images(const DrawList &out) {
  return static_cast<std::size_t>(std::ranges::count_if(
      out.world, [](const auto &value) {
        const auto* mesh=std::get_if<TriangleMesh>(&value);
        return std::holds_alternative<Image>(value)||(mesh&&mesh->texture);
      }));
}

void frame_and_blend() {
  const auto data = catalog();
  const auto frame = galaxy_artwork_world_frame(data.system_positions,
                                                 data.galactic_core, 21.);
  require(std::abs(frame.width / frame.height - 1456. / 816.) < 1e-9 && frame.width > 300.,
          "Galaxy frame stretched the approved wide spiral artwork.");
  for (const auto point : data.system_positions) {
    const auto dx = point.x - (frame.left + frame.width * .5);
    const auto dy = (point.y - (frame.top + frame.height * .47)) / .54;
    require(std::hypot(dx, dy) < frame.width * .5 * .80 / 1.04 + .001,
            "Galaxy frame does not contain its canonical catalog.");
  }
  for (const auto fitted : {.001, .1, 1., 10., 1000.}) {
    require(galaxy_overview_blend(fitted, fitted) == 1.,
            "Every native fitted scale must show the overview.");
    require(galaxy_overview_blend(fitted * 5., fitted) == 0.,
            "Five-times relative zoom must hide overview galaxies.");
  }
  const auto camera = galaxy_artwork_fit_camera(frame, 1280, 720);
  const auto top_left = camera.project({frame.left, frame.top}, 1280, 720);
  const auto bottom_right = camera.project({frame.left + frame.width,
                                             frame.top + frame.height}, 1280, 720);
  require(top_left.x >= 0 && top_left.y >= 0 && bottom_right.x <= 1280 &&
              bottom_right.y <= 720,
          "Fitted artwork is cropped by the initial viewport.");
}

void cached_assets_and_alpha(const std::filesystem::path &root) {
  NativeGalaxyBackdropAssets assets(root);
  const auto galaxy = assets.galaxy_layer();
  const auto deep = assets.deep_field();
  require(deep->width()==2944 && deep->height()==1648,
          "Approved deep-space background lost its original resolution.");
  require(deep->pixels()==decode_rgba_image(root/"assets/visual/space/deep-field-v3.png")->pixels(),
          "Overview deep field was replaced by regional stars.");
  const auto nebula = assets.regional_nebula();
  require(assets.decoded_count() == 3, "Each approved galaxy source must decode once.");
  require(assets.galaxy_layer() == galaxy && assets.deep_field() == deep &&
              assets.regional_nebula() == nebula && assets.decoded_count() == 3,
          "Galaxy sources were decoded more than once.");
  const auto &pixels = galaxy->pixels();
  const auto alpha = [&](int x, int y) {
    return pixels[(static_cast<std::size_t>(y) * galaxy->width() + x) * 4u + 3u];
  };
  require(alpha(0, 0) < 8 && alpha(galaxy->width() - 1, 0) < 8 &&
              alpha(0, galaxy->height() - 1) < 8,
          "Approved galaxy layer has an opaque rectangular border.");
  for (int x = 0; x < galaxy->width(); ++x)
    require(alpha(x, 0) == 0 && alpha(x, galaxy->height() - 1) == 0,
            "Galaxy top or bottom edge left a visible frame.");
  for (int y = 0; y < galaxy->height(); ++y)
    require(alpha(0, y) == 0 && alpha(galaxy->width() - 1, y) == 0,
            "Galaxy side edge left a visible frame.");
  require(alpha(galaxy->width() / 2, galaxy->height() / 2) > 200,
          "Approved galaxy layer lacks its visible center.");
  const auto fog=assets.undisclosed_core_fog();
  require(fog->width()==1024&&fog->height()==573,
          "Central gas lost its filament detail.");
  for(std::size_t i=3;i<fog->pixels().size();i+=4)
    require(fog->pixels()[i]<190,"Central gas became an opaque white cloud.");
}

void all_galaxy_sizes_fit_without_changing_distances(const std::filesystem::path &root) {
  using namespace stellar::core;
  const auto catalog = load_nearby_catalog(root / "data/astronomy/hyg-nearby-500-v1.json");
  double previous_width = 0.;
  for (const int count : {250, 500, 1000, 2500}) {
    const auto stars = generate_stellar_catalog(9172026, count, catalog);
    const auto core = full_galaxy_core(count);
    std::vector<WorldPoint> points;
    for (const auto& star : stars) points.push_back({star.position.x, star.position.y});
    const auto original = points;
    const auto frame = galaxy_artwork_world_frame(points, WorldPoint{core.position.x, core.position.y}, core.exclusion_radius);
    require(frame.width > previous_width, "Larger star counts did not grow the fitted galaxy.");
    previous_width = frame.width;
    require(std::abs(full_galaxy_radius(count) * full_galaxy_radius(count) / count - 32.768) < .001,
            "Galaxy area did not track population to preserve spacing.");
    for (std::size_t i = 0; i < points.size(); ++i) {
      require(points[i].x == original[i].x && points[i].y == original[i].y,
              "Artwork fitting moved an authoritative system.");
      const auto dx = points[i].x - core.position.x;
      const auto dy = (points[i].y - core.position.y) / .54;
      require(std::hypot(dx, dy) < frame.width * .4,
              "A system lies outside the spiral's visible disc.");
    }
    for (const auto extent : {std::pair{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}) {
      const auto camera = galaxy_artwork_fit_camera(frame, extent.first, extent.second);
      for (const auto point : points) {
        const auto projected = camera.project(point, extent.first, extent.second);
        require(projected.x >= 0 && projected.x <= extent.first && projected.y >= 0 && projected.y <= extent.second,
                "Fitted galaxy cropped a system at a supported resolution.");
      }
    }
  }
}

void overview_regional_and_view_gate(const std::filesystem::path &root) {
  NativeGalaxyBackdropAssets assets(root);
  NativeGalaxyBackdrop backdrop(assets);
  backdrop.bind(catalog());
  require(backdrop.regional_point_count() == 0,
          "Decorative parallax stars clash with the stationary supplied background.");
  DrawList overview;
  const auto fitted = backdrop.fit_camera(1280, 720);
  backdrop.append(overview, {7, 1280, 720, fitted, fitted.pixels_per_world, true});
  require(images(overview) == 3,
          "Overview must contain deep field, galaxy layer and undisclosed-core fog.");
  require(overview.world.size() == 3,
          "External galaxies must not be mixed with regional points at far zoom.");
  const auto overview_stats=backdrop.last_render_stats();
  require(overview_stats.deep_field_images==1&&overview_stats.star_background_images==0&&overview_stats.galaxy_layer_images==1&&
              overview_stats.regional_nebula_images==0&&overview_stats.regional_points==0,
          "Overview command statistics do not match emitted scenery.");
  const auto deep = std::get<Image>(overview.world.front());
  require(deep.resource == assets.deep_field() && deep.tint.r == 255 && deep.tint.a == 148 &&
              deep.destination.x <= 0 && deep.destination.y <= 0 &&
              deep.destination.x + deep.destination.width >= 1280 &&
              deep.destination.y + deep.destination.height >= 720,
          "Deep field must cover the viewport without a framed edge.");

  DrawList regional;
  backdrop.append(regional, {7, 1920, 1080, {{40., -18.}, .7}, .1, true});
  require(images(regional) == 3 && regional.world.size() == 3,
          "Regional view must retain the fixed stars behind its nebula and fog.");
  const auto regional_stats=backdrop.last_render_stats();
  require(regional_stats.deep_field_images==0&&regional_stats.star_background_images==1&&regional_stats.galaxy_layer_images==0&&
              regional_stats.regional_nebula_images==1&&regional_stats.regional_points==0,
          "Regional command statistics do not match emitted scenery.");
  const auto &regional_image = std::get<Image>(regional.world[1]);
  require(regional_image.destination.x < 0 && regional_image.destination.y < 0 &&
              regional_image.destination.x + regional_image.destination.width > 1920 &&
              regional_image.destination.y + regional_image.destination.height > 1080 &&
              regional_image.tint.a == 143,
          "Regional nebula did not preserve 1.12 cover and 0.56 opacity.");
  require(regional_image.resource == assets.regional_nebula() &&
              regional_image.resource != assets.deep_field() &&
              regional_image.resource != assets.galaxy_layer(),
          "Regional view used an external-galaxy image.");
  const auto first = regional.world;
  DrawList repeated;
  backdrop.append(repeated, {7, 1920, 1080, {{40., -18.}, .7}, .1, true});
  require(repeated.world.size() == first.size(), "Regional presentation is not deterministic.");
  const auto &a = std::get<Image>(first.front());
  const auto &b = std::get<Image>(repeated.world.front());
  require(a.resource == b.resource && a.destination.x == b.destination.x && a.destination.y == b.destination.y,
          "Fixed background changed between frames.");
  DrawList panned;
  backdrop.append(panned, {7, 1920, 1080, {{1040., -618.}, .7}, .1, true});
  const auto &moved_nebula = std::get<Image>(panned.world[1]);
  require(moved_nebula.destination.x != regional_image.destination.x &&
              std::abs(moved_nebula.destination.x - regional_image.destination.x) <= 36.f &&
              std::abs(moved_nebula.destination.y - regional_image.destination.y) <= 36.f,
          "Regional nebula did not use the preserved bounded pan drift.");

  for (const auto [width,height] : {std::pair{1280,720}, {3440,1440}, {800,1200}, {3840,2160}}) {
    std::optional<Image> fixed;
    for (const double zoom : {.5, 1., 5., 20.}) {
      DrawList scene;
      backdrop.append(scene,{7,width,height,{{zoom*710.,-zoom*314.},zoom},.1,true});
      const auto& background=std::get<Image>(scene.world.front());
      const auto& rect=background.destination;
      require(background.resource==assets.star_background() && rect.x<0 && rect.y<0 &&
              rect.x+rect.width>width && rect.y+rect.height>height && background.tint.a==255,
              "Stationary background exposed an edge or ceased being the back layer.");
      require(std::abs(rect.width/rect.height-2944.f/1648.f)<.00001f,
              "Stationary star background stretched to the window shape.");
      if(fixed)require(rect.x==fixed->destination.x && rect.y==fixed->destination.y &&
                       rect.width==fixed->destination.width && rect.height==fixed->destination.height &&
                       background.tint.r==fixed->tint.r && background.tint.a==fixed->tint.a,
                       "Map pan/zoom changed the fixed star background.");
      fixed=background;
    }
  }

  DrawList transition;
  backdrop.append(transition, {7,1920,1080,{{40.,-18.},.31},.1,true});
  const auto& transition_stars=std::get<Image>(transition.world[0]);
  const auto& transition_deep=std::get<Image>(transition.world[1]);
  require(transition_stars.resource==assets.star_background() &&
              transition_deep.resource==assets.deep_field() &&
              transition_stars.tint.a>=127 && transition_stars.tint.a<=128 &&
              transition_deep.tint.a==74,
          "Overview and regional backgrounds did not preserve their separate transition layers.");
  DrawList restored;
  backdrop.append(restored,{7,1280,720,fitted,fitted.pixels_per_world,true});
  require(std::get<Image>(restored.world.front()).resource==deep.resource &&
              backdrop.last_render_stats().star_background_images==0,
          "Zooming back out failed to restore the original galaxy background.");

  DrawList system;
  backdrop.append(system, {7, 1280, 720, {{}, .1}, .1, false});
  require(system.world.empty() && assets.decoded_count() == 4,
          "Galaxy imagery leaked into a system or planet view.");
  const auto system_stats=backdrop.last_render_stats();
  require(system_stats.deep_field_images==0&&system_stats.star_background_images==0&&system_stats.galaxy_layer_images==0&&
              system_stats.regional_nebula_images==0&&system_stats.regional_points==0,
          "System view retained stale galaxy command statistics.");
}

void secrecy_generation_and_order(const std::filesystem::path &root) {
  NativeGalaxyBackdropAssets assets(root);
  NativeGalaxyBackdrop backdrop(assets);
  backdrop.bind(catalog(11));
  bool stale = false;
  DrawList stale_scene;
  try { backdrop.append(stale_scene, {10, 1280, 720, {{}, .1}, .1, true}); }
  catch (const std::invalid_argument &) { stale = true; }
  require(stale, "Stale generation was allowed to reuse galaxy scenery.");

  auto disclosed = catalog(12); disclosed.galactic_core_discovered = true;
  backdrop.bind(std::move(disclosed));
  DrawList visible;
  backdrop.append(visible, {12, 1280, 720, {{8., 3.}, .1}, .1, true});
  require(images(visible) == 2,
          "A discovered core unexpectedly retained the observer fog layer.");
  backdrop.set_galactic_core_discovered(12, true);
  bool regressed = false;
  try { backdrop.set_galactic_core_discovered(12, false); }
  catch (const std::invalid_argument &) { regressed = true; }
  require(regressed, "Core discovery regressed within one campaign generation.");

  DrawList ordered;
  ordered.world.emplace_back(Image{assets.deep_field(), {0, 0, 10, 10}});
  ordered.world.emplace_back(Text{{5, 5}, "Gate148 marker", {10, 11, 12, 13}});
  ordered.lines.push_back({{1, 1}, {2, 2}, {1, 2, 3, 4}});
  ordered.circles.push_back({{3, 3}, 2, {4, 5, 6, 7}});
  ordered.text.push_back({{4, 4}, "Known", {8, 9, 10, 11}});
  promote_legacy_galaxy_foreground(ordered, 1);
  require(ordered.lines.empty() && ordered.circles.empty() && ordered.text.empty() &&
              ordered.world.size() == 5 &&
              std::holds_alternative<Image>(ordered.world[0]) &&
              std::holds_alternative<Line>(ordered.world[1]) &&
              std::holds_alternative<Circle>(ordered.world[2]) &&
              std::holds_alternative<Text>(ordered.world[3]) &&
              std::get<Text>(ordered.world[3]).value == "Gate148 marker" &&
              std::get<Text>(ordered.world[4]).value == "Known",
          "Canonical foreground was not ordered above the backdrop.");
}

void background_layers_and_pixels(const std::filesystem::path &root) {
  NativeGalaxyBackdropAssets reference(root);
  auto queue=std::make_shared<ImagePreparationQueue>(6,
      NativeGalaxyBackdropAssets::maximum_deep_field_bytes*2+
      NativeGalaxyBackdropAssets::maximum_prepared_image_bytes*2+4u*1024u*1024u+4u);
  WorkerGate gate;
  auto barrier=*queue->submit(4,[state=gate.state]{return WorkerGate::block(state);});
  gate.wait_started();
  NativeGalaxyBackdropAssets assets(root);
  assets.use_background_preparation(queue);
  NativeGalaxyBackdrop backdrop(assets);
  backdrop.bind(catalog());
  const auto fitted=backdrop.fit_camera(1280,720);
  const GalaxyBackdropView overview{7,1280,720,fitted,fitted.pixels_per_world,true};
  const GalaxyBackdropView regional{7,1920,1080,{{40.,-18.},.7},.1,true};
  DrawList scene;
  backdrop.append(scene,{7,1280,720,fitted,fitted.pixels_per_world,false});
  require(scene.world.empty()&&assets.pending_count()==0&&backdrop.artwork_ready(),
          "Non-galaxy view requested or waited for galaxy images.");
  backdrop.append(scene,overview);
  require(!backdrop.artwork_ready()&&assets.pending_count()==3&&images(scene)==0&&
              backdrop.last_render_stats().undisclosed_core_fog_images==0,
          "Pending overview generated its core gas on the UI thread or claimed finished scenery.");
  scene={};backdrop.append(scene,overview);
  require(assets.pending_count()==3,"Repeated frame admitted duplicate galaxy decodes.");
  scene={};backdrop.append(scene,regional);
  require(!backdrop.artwork_ready()&&assets.pending_count()==5&&images(scene)==0&&
              backdrop.last_render_stats().regional_points==0&&
              backdrop.last_render_stats().undisclosed_core_fog_images==0,
          "Pending regional view blocked on core gas or restored moving decorative stars.");
  gate.release();await([&]{return barrier.ready();});barrier.cancel();
  await([&]{scene={};backdrop.append(scene,regional);return backdrop.artwork_ready();});
  require(assets.decoded_count()==4&&assets.pending_count()==0&&images(scene)==3,
          "Changing view stranded completed galaxy requests or skipped scenery.");
  scene={};backdrop.append(scene,overview);
  require(backdrop.artwork_ready()&&images(scene)==3,"Ready overview did not restore all image layers.");
  const auto equal=[](const auto &a,const auto &b){return a->width()==b->width()&&a->height()==b->height()&&a->pixels()==b->pixels();};
  require(equal(assets.deep_field(),reference.deep_field())&&
              equal(assets.star_background(),reference.star_background())&&
              equal(assets.galaxy_layer(),reference.galaxy_layer())&&
              equal(assets.regional_nebula(),reference.regional_nebula())&&
              equal(assets.undisclosed_core_fog(),reference.undisclosed_core_fog()),
          "Background galaxy preparation changed final source RGBA.");
  backdrop.discard_campaign();backdrop.bind(catalog(8));
  require(assets.pending_count()==0&&assets.decoded_count()==4&&
              assets.request_deep_field()==assets.deep_field(),
          "Campaign discard unnecessarily lost reusable generic scenery.");
}

void background_capacity_cancel_and_failure(const std::filesystem::path &root) {
  auto queue=std::make_shared<ImagePreparationQueue>(2,NativeGalaxyBackdropAssets::maximum_deep_field_bytes+4u);
  WorkerGate gate;
  auto barrier=*queue->submit(4,[state=gate.state]{return WorkerGate::block(state);});
  gate.wait_started();
  NativeGalaxyBackdropAssets assets(root);assets.use_background_preparation(queue);
  NativeGalaxyBackdrop backdrop(assets);backdrop.bind(catalog());
  require(!assets.request_deep_field()&&assets.pending_count()==1,"Cold deep field was not queued.");
  require(!assets.request_regional_nebula()&&assets.pending_count()==1,"Galaxy requests exceeded reserved output capacity.");
  require(!assets.request_undisclosed_core_fog()&&assets.pending_count()==1,"Core gas exceeded shared preparation capacity.");
  backdrop.discard_campaign();
  require(assets.pending_count()==0&&assets.decoded_count()==0&&backdrop.artwork_ready(),
          "Discard retained a queued galaxy decode or pending capture.");
  gate.release();await([&]{return barrier.ready();});barrier.cancel();
  std::shared_ptr<const RgbaImage> result;
  await([&]{result=assets.request_regional_nebula();return static_cast<bool>(result);});
  require(assets.decoded_count()==1&&assets.pending_count()==0,
          "Canceled galaxy requests repopulated the cache or prevented a capacity retry.");
  require(!assets.request_undisclosed_core_fog()&&assets.pending_count()==1,"Core gas was not prepared asynchronously.");
  assets.cancel_preparation();
  require(assets.pending_count()==0,"Canceled core gas retained its preparation ticket.");
  await([&]{return static_cast<bool>(assets.request_undisclosed_core_fog());});
  require(assets.pending_count()==0,"Core gas did not recover after cancellation.");
  bool owner_rejected{};
  std::jthread other([&]{try{(void)assets.request_deep_field();}catch(const std::logic_error&){owner_rejected=true;}});
  other.join();require(owner_rejected,"Galaxy request accepted a non-owner thread.");
  NativeGalaxyBackdropAssets missing(root/"nonexistent-galaxy-fixture");
  missing.use_background_preparation(queue);
  require(!missing.request_deep_field(),"Missing source request blocked instead of deferring.");
  bool failed{};
  try{await([&]{return static_cast<bool>(missing.request_deep_field());});}
  catch(const std::runtime_error &error){const std::string message=error.what();failed=message.find("deep-field-v3.png")!=std::string::npos&&message.find("nonexistent-galaxy-fixture")!=std::string::npos;}
  require(failed&&missing.pending_count()==0&&missing.decoded_count()==0,
          "Background galaxy failure lacked its path or retained a pending request.");
}

void supplied_morphology_layers(const std::filesystem::path& root) {
  NativeGalaxyBackdropAssets assets(root);NativeGalaxyBackdrop backdrop(assets);
  for(int m=0;m<6;++m){
    const auto morphology=static_cast<stellar::core::GalaxyMorphology>(m);
    const auto pair=stellar::core::galaxy_visual_pair(morphology,stellar::core::PopulationState::Active);
    const auto f=stellar::core::galaxy_footprint_frame(morphology,500);
    auto view=catalog(120+m);view.map_asset_path=pair.map_path;view.use_spiral_artwork=false;
    view.fixed_artwork_frame=GalaxyBackdropFrame{f.left,f.top,f.width,f.height};view.galactic_core.reset();
    backdrop.bind(view);const auto layer=assets.galaxy_layer();
    const auto pixels=layer->pixels();
    for(int x=0;x<layer->width();++x)require(pixels[x*4+3]==0,"New galaxy art retains a rectangular matte");
    DrawList draw;const auto camera=backdrop.fit_camera(1920,1080);
    backdrop.append(draw,{view.campaign_generation,1920,1080,camera,camera.pixels_per_world,true});
    require(std::ranges::any_of(draw.world,[&](const auto& item){const auto* image=std::get_if<Image>(&item);return image&&image->resource==layer;}),"Correct supplied gas-dust layer was not rendered");
    require(backdrop.artwork_frame()&&backdrop.artwork_frame()->width==f.width,"Renderer did not preserve the generator footprint");
    for(const auto size:std::array<std::pair<int,int>,5>{{{1280,720},{1920,1080},{2560,1440},{3440,1440},{3840,2160}}}){
      const float scale=static_cast<float>(size.second)/1080.f;
      const UiRect region{78*scale,110*scale,size.first-442*scale,size.second-206*scale};
      const auto fitted=backdrop.fit_camera(size.first,size.second,region);
      const auto first=fitted.project({f.left,f.top},size.first,size.second);
      const auto last=fitted.project({f.left+f.width,f.top+f.height},size.first,size.second);
      require(region.contains(first)&&region.contains(last),"Galaxy artwork is clipped by the HUD at overview zoom");
    }
  }
}
void oversized_artwork_is_never_cached() {
  for (const int kind : {0, 1, 2}) {
  OversizeArtworkFixture fixture(kind);
  NativeGalaxyBackdropAssets synchronous(fixture.root());
  const auto load=[&]{return kind == 0 ? synchronous.deep_field() : kind == 1 ? synchronous.star_background() : synchronous.regional_nebula();};
  bool first_rejected{},second_rejected{};
  try{(void)load();}catch(const std::runtime_error&){first_rejected=true;}
  try{(void)load();}catch(const std::runtime_error&){second_rejected=true;}
  require(first_rejected&&second_rejected&&synchronous.decoded_count()==0,
          "Oversized synchronous artwork was cached after rejection.");
  auto queue=std::make_shared<ImagePreparationQueue>();
  NativeGalaxyBackdropAssets asynchronous(fixture.root());
  asynchronous.use_background_preparation(queue);
  const auto request=[&]{return kind == 0 ? asynchronous.request_deep_field() : kind == 1 ? asynchronous.request_star_background() : asynchronous.request_regional_nebula();};
  require(!request(),"Oversized artwork request did not defer.");
  bool failed{};
  try{await([&]{return static_cast<bool>(request());});}
  catch(const std::runtime_error &error){const std::string message=error.what();failed=message.find(kind == 0 ? "deep-field-v3.png" : kind == 1 ? "star-background.png" : "regional-nebula-b.png")!=std::string::npos&&message.find("prepared image byte limit")!=std::string::npos;}
  require(failed&&asynchronous.pending_count()==0&&asynchronous.decoded_count()==0,
          "Oversized background artwork failure was cached or retained pending state.");
  }
}
}  // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2 && argc != 3) throw std::invalid_argument("usage: test_native_galaxy_backdrop <asset-root> [cooked-root]");
    frame_and_blend();
    const std::filesystem::path root(argv[1]);
    cached_assets_and_alpha(root);
    all_galaxy_sizes_fit_without_changing_distances(root);
    overview_regional_and_view_gate(root);
    secrecy_generation_and_order(root);
    background_layers_and_pixels(root);
    background_capacity_cancel_and_failure(root);
    oversized_artwork_is_never_cached();
    supplied_morphology_layers(root);
    if(argc==3){
      NativeGalaxyBackdropAssets loose(root);
      const auto expected=loose.star_background();
      stellar::engine::mount_asset_registry(argv[2],false);
      NativeGalaxyBackdropAssets cooked(argv[2]);
      const auto actual=cooked.star_background();
      require(actual->pixels()==expected->pixels()&&actual->byte_size()==expected->byte_size()&&actual->cooked_mips().empty(),
              "Cooked 2D backdrop changed source pixels or retained unused mips.");
      background_layers_and_pixels(argv[2]);
      stellar::engine::unmount_asset_registry();
      std::cout << "Cooked background parity and bounded preparation passed\n";
    }
    std::cout << "native galaxy backdrop: 9/9 cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native galaxy backdrop failure: " << error.what() << '\n';
    return 1;
  }
}
