#include "native_galaxy_star_markers.hpp"
#include <stellar/engine/native_triangle_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_galaxy_ui;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
const Image &image_at(const DrawList &draw, std::size_t index) {
  const auto *image = std::get_if<Image>(&draw.world.at(index));
  if (!image) throw std::runtime_error("expected ordered galaxy marker image");
  return *image;
}
const Circle &circle_at(const DrawList &draw, std::size_t index) {
  const auto *circle = std::get_if<Circle>(&draw.world.at(index));
  if (!circle) throw std::runtime_error("expected ordered galaxy marker circle");
  return *circle;
}
std::uint8_t alpha(const RgbaImage &image, int x, int y) {
  return image.pixels().at(
      static_cast<std::size_t>((y * image.width() + x) * 4 + 3));
}
std::uint8_t red(const RgbaImage &image, int x, int y) {
  return image.pixels().at(
      static_cast<std::size_t>((y * image.width() + x) * 4));
}
void require_transparent_border(const RgbaImage &image) {
  for (int index = 0; index < image.width(); ++index) {
    require(alpha(image, index, 0) == 0 &&
                alpha(image, index, image.height() - 1) == 0 &&
                alpha(image, 0, index) == 0 &&
                alpha(image, image.width() - 1, index) == 0,
            "galaxy marker outer border was not fully transparent");
  }
}

void shared_discrete_resources() {
  NativeGalaxyStarMarkerRenderer renderer;
  DrawList first;
  renderer.append(first, {120, 90}, 2.f,
                  {GalaxyStarVisualClass::g_yellow_dwarf}, false);
  require(first.world.size() == 2 &&
              std::holds_alternative<Circle>(first.world[0]) &&
              std::holds_alternative<Image>(first.world[1]),
          "single star did not order its contrast disc before its image");
  const auto resource = image_at(first, 1).resource;
  constexpr int size = NativeGalaxyStarMarkerRenderer::texture_size;
  constexpr int center = size / 2;
  require(resource->width() == size && resource->height() == size,
          "galaxy marker resource exceeded its fixed resolution tier");
  require(alpha(*resource, 0, 0) == 0 && alpha(*resource, center, center) > 250,
          "galaxy marker has a square frame or lacks a crisp core");
  require_transparent_border(*resource);
  require(alpha(*resource, center + 6, center + 4) > 245 &&
              alpha(*resource, center + 24, center + 16) < 35,
          "stellar core faded gradually into a blurred patch instead of a short luminous limb");
  require(alpha(*resource, size / 8, center) > 14 &&
              alpha(*resource, size / 8, center) > alpha(*resource, size / 8, size * 3 / 8),
          "thin horizontal diffraction ray was not antialiased visibly");
  require(alpha(*resource, center, size / 8) > 6 &&
              alpha(*resource, center, size / 8) > alpha(*resource, size * 3 / 8, size / 8),
          "thin vertical diffraction ray was not antialiased visibly");
  require(red(*resource, size / 8, center) > 240,
          "faint ray color was darkened a second time before alpha blending");
  require(alpha(*resource, center + 48, center + 32) < 10,
          "a broad contrast ring or dense halo dulled the star's silhouette");
  const auto core_pixel = static_cast<std::size_t>((center * size + center) * 4);
  require(resource->pixels()[core_pixel] > 245 &&
              resource->pixels()[core_pixel + 1] > 245 &&
              resource->pixels()[core_pixel + 2] > 245,
          "star lost its brilliant white-hot center");
  DrawList moved;
  renderer.append(moved, {410, 330}, 5.f,
                  {GalaxyStarVisualClass::g_yellow_dwarf}, false);
  require(image_at(moved, 1).resource == resource &&
              renderer.stats().generated_resources == 1,
          "position, scale, or system identity regenerated a shared class marker");
}

void compact_and_multiplicity_cues() {
  NativeGalaxyStarMarkerRenderer renderer;
  DrawList unknown;
  renderer.append(unknown, {50, 50}, 2.f, {}, false);
  require(unknown.world.size() == 2 &&
              std::holds_alternative<Circle>(unknown.world[0]) &&
              std::holds_alternative<Image>(unknown.world[1]),
          "unknown observer marker invented multiplicity or compact-object cues");
  DrawList black_hole;
  renderer.append(black_hole, {50, 50}, 3.f,
                  {GalaxyStarVisualClass::black_hole}, false);
  const auto &hole = *image_at(black_hole, 1).resource;
  const auto center = static_cast<std::size_t>(((hole.height() / 2) * hole.width() + hole.width() / 2) * 4);
  require(hole.pixels()[center] < 80 && hole.pixels()[center + 1] < 80,
          "black-hole marker did not retain its dark center");
  DrawList pulsar;
  renderer.append(pulsar, {50, 50}, 3.f,
                  {GalaxyStarVisualClass::pulsar}, false);
  require(image_at(pulsar, 1).resource != image_at(black_hole, 1).resource,
          "pulsar and black-hole markers collapsed to one style");
  DrawList triple;
  renderer.append(triple, {50, 50}, 3.f,
                  {GalaxyStarVisualClass::g_yellow_dwarf,
                   GalaxyStarVisualClass::m_red_dwarf,
                   GalaxyStarVisualClass::white_dwarf}, false);
  require(triple.world.size() == 4 &&
              std::holds_alternative<Circle>(triple.world[0]) &&
              std::ranges::all_of(triple.world.begin() + 1,
                                  triple.world.end(),
                                  [](const WorldCommand &command) {
                                    return std::holds_alternative<Image>(command);
                                  }),
          "observer-approved triple did not render exactly three stellar images");
}

void hard_budget_and_validation() {
  NativeGalaxyStarMarkerRenderer renderer;
  constexpr GalaxyStarVisualClass visuals[] = {
      GalaxyStarVisualClass::unknown, GalaxyStarVisualClass::m_red_dwarf,
      GalaxyStarVisualClass::k_orange_dwarf,
      GalaxyStarVisualClass::g_yellow_dwarf,
      GalaxyStarVisualClass::f_yellow_white_dwarf,
      GalaxyStarVisualClass::a_white_star, GalaxyStarVisualClass::hot_blue_star,
      GalaxyStarVisualClass::giant, GalaxyStarVisualClass::white_dwarf,
      GalaxyStarVisualClass::neutron_star, GalaxyStarVisualClass::black_hole,
      GalaxyStarVisualClass::protostar, GalaxyStarVisualClass::pulsar};
  DrawList draw;
  for (const auto visual : visuals) {
    renderer.append(draw, {0, 0}, 1.f, {visual}, false);
    require_transparent_border(*image_at(draw, draw.world.size() - 1).resource);
  }
  renderer.append_neutral_batch(draw,{0,0},1.f,false,std::nullopt,1.f);
  const auto stats = renderer.stats();
  require(stats.cached_resources == std::size(visuals)+1 &&
              stats.cached_resources ==
                  NativeGalaxyStarMarkerRenderer::maximum_cached_resources &&
              stats.cached_bytes ==
                  NativeGalaxyStarMarkerRenderer::maximum_cached_bytes,
          "fixed canonical marker palette did not match its hard cache budget");
  bool rejected{};
  try {
    renderer.append(draw, {0, 0}, std::numeric_limits<float>::quiet_NaN(), {},
                    false);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "nonfinite galaxy marker geometry was accepted");
  renderer.clear();
  require(renderer.stats().cached_resources == 0 && renderer.stats().cached_bytes == 0,
          "marker cache clear retained resources");
}

void unexplored_alpha_preserves_resources_and_dims_every_component() {
  NativeGalaxyStarMarkerRenderer renderer;
  const NativeGalaxyStarAppearance appearance{
      GalaxyStarVisualClass::g_yellow_dwarf, GalaxyStarVisualClass::m_red_dwarf,
      GalaxyStarVisualClass::white_dwarf};
  DrawList visible;
  renderer.append(visible, {100, 100}, 4.f, appearance, true);
  DrawList dimmed;
  renderer.append(dimmed, {100, 100}, 4.f, appearance, true, std::nullopt, .4f);
  require(visible.world.size() == 5 && dimmed.world.size() == 5,
          "selected triple marker did not retain every visual component while dimmed");
  require(circle_at(dimmed, 0).color.a == 21 && circle_at(dimmed, 1).color.a == 62,
          "selection or contrast disc ignored unexplored alpha");
  for (std::size_t index = 2; index != 5; ++index) {
    const auto &full = image_at(visible, index);
    const auto &dim = image_at(dimmed, index);
    require(dim.resource == full.resource && dim.tint.a == 102,
            "primary, secondary, or tertiary marker did not share and dim its immutable image");
    require_transparent_border(*dim.resource);
  }
  require(renderer.stats().generated_resources == 3,
          "unexplored alpha regenerated marker resources");

  DrawList invisible;
  renderer.append(invisible, {100, 100}, 4.f, appearance, true, std::nullopt, 0.f);
  require(std::ranges::all_of(invisible.world, [](const WorldCommand &command) {
    if (const auto *circle = std::get_if<Circle>(&command)) return circle->color.a == 0;
    if (const auto *image = std::get_if<Image>(&command)) return image->tint.a == 0;
    return false;
  }), "zero unexplored alpha emitted a visible marker primitive");

  for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), -.01f}) {
    bool rejected{};
    try { renderer.append(invisible, {100, 100}, 4.f, appearance, false, std::nullopt, invalid); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "invalid unexplored alpha was accepted");
  }
}

void dense_neutral_batch_preserves_geometry_and_order() {
  NativeGalaxyStarMarkerRenderer renderer;
  const UiRect clip{0,0,1920,1080};
  DrawList reference,batch;
  renderer.append(reference,{100,80},2.f,{},true,clip,.4f);
  renderer.append_neutral_batch(batch,{100,80},2.f,true,clip,.4f);
  const auto& mesh=std::get<TriangleMesh>(batch.world.front());
  validate_triangle_mesh(mesh);
  require(mesh.vertices.size()==48&&mesh.indices.size()==126,"Batched selection/contrast/star primitives changed");
  for(int part=0;part<2;++part){
    const auto& circle=circle_at(reference,part);const auto offset=static_cast<std::size_t>(part*22);
    require(mesh.vertices[offset].x==circle.center.x&&mesh.vertices[offset].y==circle.center.y&&
            mesh.vertex_colors[offset].a==circle.color.a&&mesh.vertex_colors[offset+1].a==0,
            "Batched circle changed center, opacity or soft edge");
    require(mesh.vertices[offset+1].x==circle.center.x+circle.radius,"Batched circle changed radius");
  }
  const auto& sprite=image_at(reference,2);
  require(mesh.vertices[44].x==sprite.destination.x&&mesh.vertices[44].y==sprite.destination.y&&
          mesh.vertex_colors[44].a==sprite.tint.a,"Batched marker changed placement or opacity");
  // The complete original star texture must survive packing byte-for-byte.
  for(int y=0;y<sprite.resource->height();++y)
    require(std::equal(sprite.resource->pixels().begin()+y*256*4,sprite.resource->pixels().begin()+(y+1)*256*4,
                       mesh.texture->pixels().begin()+y*mesh.texture->width()*4),"Atlas packing altered the light profile");
  DrawList dense;
  for(int i=0;i<10000;++i)renderer.append_neutral_batch(dense,{float(i%100)*10,float(i/100)*10},1.25f,false,clip,.86f);
  require(dense.world.size()<=4,"Ten thousand neutral markers did not batch into bounded submissions");
  std::size_t vertices=0,indices=0;
  for(const auto& command:dense.world){const auto& m=std::get<TriangleMesh>(command);validate_triangle_mesh(m);vertices+=m.vertices.size();indices+=m.indices.size();}
  require(vertices==260000&&indices==660000,"Dense batching dropped or duplicated stars");
  const auto before=dense.world.size();
  dense.world.emplace_back(Line{{0,0},{1,1},{255,255,255,255}});
  renderer.append_neutral_batch(dense,{1,1},1.f,false,clip,1.f);
  renderer.append_neutral_batch(dense,{1,1},1.f,false,UiRect{0,0,800,600},1.f);
  require(dense.world.size()==before+3&&std::holds_alternative<Line>(dense.world[before]),"Batch crossed a layer or clipping boundary");
}

void compact_large_catalog_keeps_every_system(){
  NativeGalaxyStarMarkerRenderer renderer;DrawList draw;const UiRect clip{0,0,1920,1080};
  for(int i=0;i<50000;++i)renderer.append_neutral_batch(draw,{float(i%250)*7,float(i/250)*5},.6f,false,clip,.55f,std::nullopt,true);
  std::size_t vertices=0,indices=0;
  for(const auto& command:draw.world){const auto& mesh=std::get<TriangleMesh>(command);validate_triangle_mesh(mesh);vertices+=mesh.vertices.size();indices+=mesh.indices.size();}
  require(draw.world.size()==4&&vertices==200000&&indices==300000,
      "Compact 50,000-system map dropped markers or exceeded batch budgets.");
  const auto& first=std::get<TriangleMesh>(draw.world.front());
  require(first.vertex_colors.front().a==140&&first.texture_coordinates.front().x>0&&
      first.texture_coordinates.front().y==.25f,"Compact markers lost dimming or their cropped core.");
  const auto last_count=std::get<TriangleMesh>(draw.world.back()).vertices.size();
  renderer.append_neutral_batch(draw,{50,50},2.f,true,clip,1.f,std::nullopt,true);
  require(std::get<TriangleMesh>(draw.world.back()).vertices.size()==last_count+48,
      "Selected star lost its full marker and selection halo at low detail.");
}

} // namespace

int main() try {
  for(const int height:{720,1080,1440,2160}){
    const auto start=galaxy_star_core_radius(1.,height);
    const auto near=galaxy_star_core_radius(5.,height);
    const auto close=galaxy_star_core_radius(30.,height);
    const auto detail=galaxy_star_core_radius(10000.,height);
    require(detail>=height*.25f&&detail<=height*.45f,"Deep map zoom must expose enough surface detail without a screen-filling unbounded sprite");
    require(start>=1.f&&start<=2.5f&&near>start*2.f&&close>near*2.f,"star markers stayed tiny or failed to grow with zoom");
    require(galaxy_star_core_radius(5.,height,GalaxyStarVisualClass::giant)>near*1.5f,"giant marker lost its larger silhouette");
    NativeGalaxyStarMarkerRenderer renderer;DrawList a,b;
    renderer.append(a,{100,100},start,{},false);renderer.append(b,{100,100},close,{},false);
    require(image_at(b,1).destination.width>image_at(a,1).destination.width*4.f,"zoom did not enlarge drawn stars");
    require(image_at(a,1).resource==image_at(b,1).resource,"zoom allocated a new star texture");
  }
  shared_discrete_resources();
  compact_and_multiplicity_cues();
  hard_budget_and_validation();
  unexplored_alpha_preserves_resources_and_dims_every_component();
  dense_neutral_batch_preserves_geometry_and_order();
  compact_large_catalog_keeps_every_system();
  std::cout << "native galaxy star marker tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
