#include "native_galaxy_backdrop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace stellar::native_galaxy_ui {
namespace {
using namespace stellar::native_map;

constexpr std::size_t regional_star_count = 260;
constexpr std::size_t clustered_star_count = 96;
constexpr double vertical_disc_ratio = .72;
constexpr double visible_disc_half_width = .81818182;

[[nodiscard]] std::string utf8(const std::filesystem::path &path) {
  const auto encoded = path.u8string();
  return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

[[nodiscard]] bool finite_point(const WorldPoint point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] UiRect cover(const RgbaImage &image, int width, int height) {
  const auto scale = std::max(static_cast<float>(width) / image.width(),
                              static_cast<float>(height) / image.height());
  const auto w = image.width() * scale;
  const auto h = image.height() * scale;
  return {(width - w) * .5f, (height - h) * .5f, w, h};
}

[[nodiscard]] UiRect regional_cover(const RgbaImage &image, int width,
                                    int height, const Camera &camera) {
  const auto scale = std::max(static_cast<float>(width) / image.width(),
                              static_cast<float>(height) / image.height()) * 1.12f;
  const auto w = image.width() * scale;
  const auto h = image.height() * scale;
  const auto pan_x = -camera.center.x * camera.pixels_per_world;
  const auto pan_y = -camera.center.y * camera.pixels_per_world;
  const auto drift_x = static_cast<float>(std::sin(pan_x * .0007) * 18.);
  const auto drift_y = static_cast<float>(std::sin(pan_y * .0007) * 18.);
  return {(width - w) * .5f + drift_x, (height - h) * .5f + drift_y, w, h};
}

[[nodiscard]] bool intersects(const UiRect a, const UiRect b) {
  return a.x < b.x + b.width && b.x < a.x + a.width &&
         a.y < b.y + b.height && b.y < a.y + a.height;
}

[[nodiscard]] std::shared_ptr<const RgbaImage> make_core_fog() {
  constexpr int size = 192;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4u);
  const auto hash = [](std::int32_t x, std::int32_t y) {
    auto value = static_cast<std::uint32_t>(x) * 0x9e3779b9u ^
                 static_cast<std::uint32_t>(y) * 0x85ebca6bu ^ 41729u;
    value ^= value >> 16u; value *= 0x7feb352du;
    value ^= value >> 15u; value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0xffffu) / 65535.f;
  };
  const auto noise = [&](float x, float y, float frequency) {
    x *= frequency; y *= frequency;
    const auto ix = static_cast<std::int32_t>(std::floor(x));
    const auto iy = static_cast<std::int32_t>(std::floor(y));
    auto fx = x - std::floor(x), fy = y - std::floor(y);
    fx = fx * fx * (3.f - 2.f * fx); fy = fy * fy * (3.f - 2.f * fy);
    const auto top = std::lerp(hash(ix, iy), hash(ix + 1, iy), fx);
    const auto bottom = std::lerp(hash(ix, iy + 1), hash(ix + 1, iy + 1), fx);
    return std::lerp(top, bottom, fy);
  };
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const auto nx = (x + .5f - size * .5f) / (size * .5f);
      const auto ny = (y + .5f - size * .5f) / (size * .5f);
      const auto distance = std::hypot(nx, ny);
      const auto u = static_cast<float>(x) / size;
      const auto v = static_cast<float>(y) / size;
      const auto cloud = noise(u, v, 4.f) * .55f + noise(u + 2.7f, v - 1.4f, 9.f) * .30f +
                         noise(u - 4.1f, v + 3.3f, 19.f) * .15f;
      auto edge = std::clamp((.94f - static_cast<float>(distance) -
                              (cloud - .5f) * .10f) / .38f, 0.f, 1.f);
      edge = edge * edge * (3.f - 2.f * edge);
      const auto light = .49f + .35f * cloud +
                         .14f * std::clamp(1.f - static_cast<float>(distance) / .6f,
                                          0.f, 1.f);
      const auto i = (static_cast<std::size_t>(y) * size + x) * 4u;
      pixels[i] = static_cast<std::uint8_t>(std::clamp(light * 230.f, 0.f, 255.f));
      pixels[i + 1] = static_cast<std::uint8_t>(std::clamp(light * 240.f, 0.f, 255.f));
      pixels[i + 2] = static_cast<std::uint8_t>(std::clamp(light * 255.f, 0.f, 255.f));
      pixels[i + 3] = static_cast<std::uint8_t>(std::clamp(edge * 252.f, 0.f, 255.f));
    }
  }
  return RgbaImage::create(size, size, std::move(pixels));
}
}  // namespace

double galaxy_overview_blend(const double pixels_per_world,
                             const double fitted_pixels_per_world) {
  if (!std::isfinite(pixels_per_world) || !std::isfinite(fitted_pixels_per_world) ||
      pixels_per_world <= 0. || fitted_pixels_per_world <= 0.)
    throw std::invalid_argument("Galaxy backdrop camera scales must be finite and positive.");
  const auto relative_zoom = pixels_per_world / fitted_pixels_per_world;
  const auto progress = std::clamp((relative_zoom - 1.2) / (5. - 1.2),
                                   0., 1.);
  return 1. - progress * progress * (3. - 2. * progress);
}

GalaxyBackdropFrame galaxy_artwork_world_frame(
    const std::span<const WorldPoint> systems,
    const std::optional<WorldPoint> core,
    const double core_exclusion_radius) {
  if (systems.empty()) throw std::invalid_argument("Galaxy backdrop requires a nonempty catalog.");
  if (core && (!finite_point(*core) || !std::isfinite(core_exclusion_radius) ||
               core_exclusion_radius < 0.))
    throw std::invalid_argument("Galaxy backdrop core geometry is invalid.");
  for (const auto point : systems)
    if (!finite_point(point)) throw std::invalid_argument("Galaxy backdrop catalog coordinates must be finite.");

  WorldPoint center{};
  if (core) {
    center = *core;
  } else {
    auto min_x = systems.front().x, max_x = min_x;
    auto min_y = systems.front().y, max_y = min_y;
    for (const auto point : systems) {
      min_x = std::min(min_x, point.x); max_x = std::max(max_x, point.x);
      min_y = std::min(min_y, point.y); max_y = std::max(max_y, point.y);
    }
    center = {(min_x + max_x) * .5, (min_y + max_y) * .5};
  }
  auto required = core ? core_exclusion_radius / .14 : 0.;
  for (const auto point : systems) {
    const auto dx = point.x - center.x;
    const auto dy = (point.y - center.y) / vertical_disc_ratio;
    required = std::max(required, std::hypot(dx, dy));
  }
  const auto side = std::max(1., required * 2. * 1.04 / visible_disc_half_width);
  return {center.x - side * .5, center.y - side * .5, side, side};
}

Camera galaxy_artwork_fit_camera(const GalaxyBackdropFrame &frame,
                                 const int viewport_width,
                                 const int viewport_height,
                                 const double fill_fraction) {
  if (viewport_width <= 0 || viewport_height <= 0 ||
      !std::isfinite(frame.left) || !std::isfinite(frame.top) ||
      !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
      frame.width <= 0. || frame.height <= 0. ||
      !std::isfinite(fill_fraction) || fill_fraction <= 0. || fill_fraction > 1.)
    throw std::invalid_argument("Galaxy artwork fit geometry is invalid.");
  Camera result;
  result.center = {frame.left + frame.width * .5,
                   frame.top + frame.height * .5};
  result.pixels_per_world =
      std::min(static_cast<double>(viewport_width) / frame.width,
               static_cast<double>(viewport_height) / frame.height) *
      fill_fraction;
  return result;
}

NativeGalaxyBackdropAssets::NativeGalaxyBackdropAssets(std::filesystem::path root)
    : asset_root_(std::move(root)) {
  if (asset_root_.empty()) throw std::invalid_argument("Galaxy artwork asset root is required.");
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::deep_field() {
  if (!deep_field_) {
    const auto path = asset_root_ / "assets/visual/space/deep-field-v2.png";
    try { deep_field_ = decode_rgba_image(path); }
    catch (const std::exception &error) { throw std::runtime_error("Galaxy deep field failed to decode: " + utf8(path) + ": " + error.what()); }
    ++decoded_;
  }
  return deep_field_;
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::galaxy_layer() {
  if (!galaxy_layer_) {
    const auto path = asset_root_ / "assets/visual/space/milky-way-layer-v2.png";
    try { galaxy_layer_ = decode_rgba_image(path); }
    catch (const std::exception &error) { throw std::runtime_error("Galaxy layer failed to decode: " + utf8(path) + ": " + error.what()); }
    ++decoded_;
  }
  return galaxy_layer_;
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::regional_nebula() {
  if (!regional_nebula_) {
    const auto path = asset_root_ / "assets/visual/space/regional-nebula-b.png";
    try { regional_nebula_ = decode_rgba_image(path); }
    catch (const std::exception &error) { throw std::runtime_error("Regional nebula failed to decode: " + utf8(path) + ": " + error.what()); }
    ++decoded_;
  }
  return regional_nebula_;
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::undisclosed_core_fog() {
  if (!core_fog_) core_fog_ = make_core_fog();
  return core_fog_;
}

std::size_t NativeGalaxyBackdropAssets::decoded_count() const noexcept { return decoded_; }

NativeGalaxyBackdrop::NativeGalaxyBackdrop(NativeGalaxyBackdropAssets &assets) : assets_(&assets) {}

void NativeGalaxyBackdrop::bind(GalaxyBackdropCatalog catalog) {
  if (catalog_ && catalog.campaign_generation < catalog_->campaign_generation)
    throw std::invalid_argument("A stale campaign cannot replace the galaxy backdrop.");
  auto frame = galaxy_artwork_world_frame(catalog.system_positions, catalog.galactic_core,
                                          catalog.galactic_core_exclusion_radius);
  std::mt19937_64 random(static_cast<std::uint64_t>(catalog.campaign_seed) ^ 0x4d4150u);
  std::uniform_real_distribution<double> unit(0., 1.);
  regional_points_.clear(); regional_points_.reserve(regional_star_count + clustered_star_count);
  const auto add = [&](double x, double y, bool clustered) {
    const auto cool = unit(random);
    const auto color = cool < .16 ? Color{150,191,255,255} :
                       cool > .87 ? Color{255,214,171,255} : Color{216,229,255,255};
    regional_points_.push_back({x,y,
      static_cast<float>((clustered ? .42 : .32) + unit(random) * (clustered ? .72 : .62)),
      static_cast<float>((clustered ? .16 : .09) + unit(random) * (clustered ? .25 : .19)), color});
  };
  for (std::size_t i=0;i<regional_star_count;++i) add(unit(random),unit(random),false);
  for (int cluster=0;cluster<3;++cluster) {
    const auto cx=.18+unit(random)*.64, cy=.18+unit(random)*.64;
    for (std::size_t i=0;i<clustered_star_count/3;++i) {
      const auto angle=unit(random)*6.283185307179586, distance=std::sqrt(unit(random))*.105;
      auto x=cx+std::cos(angle)*distance, y=cy+std::sin(angle)*distance;
      x-=std::floor(x); y-=std::floor(y); add(x,y,true);
    }
  }
  catalog_ = std::move(catalog); artwork_frame_ = frame;
}

void NativeGalaxyBackdrop::set_galactic_core_discovered(
    const std::uint64_t generation, const bool discovered) {
  if (!catalog_ || catalog_->campaign_generation != generation)
    throw std::invalid_argument("Galaxy core visibility does not match the bound campaign.");
  if (catalog_->galactic_core_discovered && !discovered)
    throw std::invalid_argument("Galaxy core discovery cannot regress within a campaign.");
  catalog_->galactic_core_discovered = discovered;
}

void NativeGalaxyBackdrop::discard_campaign() noexcept { catalog_.reset(); artwork_frame_.reset(); regional_points_.clear();last_stats_={}; }

void NativeGalaxyBackdrop::append(DrawList &out, const GalaxyBackdropView &view) {
  last_stats_ = {};
  if (!view.galaxy_view) return;
  if (!catalog_ || !artwork_frame_ || view.campaign_generation != catalog_->campaign_generation)
    throw std::invalid_argument("Galaxy backdrop view does not match its bound campaign.");
  if (view.viewport_width <= 0 || view.viewport_height <= 0)
    throw std::invalid_argument("Galaxy backdrop viewport must be positive.");
  const auto blend=galaxy_overview_blend(view.camera.pixels_per_world,view.fitted_pixels_per_world);
  last_stats_.overview_blend=blend;
  const UiRect viewport{0,0,static_cast<float>(view.viewport_width),static_cast<float>(view.viewport_height)};
  if (blend > .002) {
    auto deep=assets_->deep_field();
    out.world.emplace_back(Image{deep,cover(*deep,view.viewport_width,view.viewport_height),std::nullopt,{255,255,255,static_cast<std::uint8_t>(std::lround(255.*.58*blend))},viewport});
    last_stats_.deep_field_images=1;
    const auto &frame=*artwork_frame_;
    const auto top_left=view.camera.project({frame.left,frame.top},view.viewport_width,view.viewport_height);
    const auto destination=UiRect{top_left.x,top_left.y,static_cast<float>(frame.width*view.camera.pixels_per_world),static_cast<float>(frame.height*view.camera.pixels_per_world)};
    if (intersects(destination,viewport)) {out.world.emplace_back(Image{assets_->galaxy_layer(),destination,std::nullopt,{255,255,255,static_cast<std::uint8_t>(std::lround(255.*blend))},viewport});last_stats_.galaxy_layer_images=1;}
  }
  const auto regional=std::clamp(1.-blend*2.,0.,1.);
  last_stats_.regional_opacity=regional;
  if (regional>.002) {
    auto nebula=assets_->regional_nebula();
    out.world.emplace_back(Image{nebula,
      regional_cover(*nebula,view.viewport_width,view.viewport_height,view.camera),
      std::nullopt,{255,255,255,static_cast<std::uint8_t>(std::lround(255.*.56*regional))},viewport});
    last_stats_.regional_nebula_images=1;
    const auto parallax=.012+std::min(.045,view.camera.pixels_per_world*.0012);
    const auto px=static_cast<float>(-view.camera.center.x*view.camera.pixels_per_world*parallax);
    const auto py=static_cast<float>(-view.camera.center.y*view.camera.pixels_per_world*parallax);
    const auto wrap=[](float value,float extent){value=std::fmod(value,extent);return value<0?value+extent:value;};
    for(const auto &point:regional_points_) {
      const Point position{wrap(static_cast<float>(point.unit_x*view.viewport_width)+px,static_cast<float>(view.viewport_width)),wrap(static_cast<float>(point.unit_y*view.viewport_height)+py,static_cast<float>(view.viewport_height))};
      auto color=point.color;color.a=static_cast<std::uint8_t>(std::lround(255.*point.alpha*regional));
      out.world.emplace_back(Circle{position,point.radius,color});
      ++last_stats_.regional_points;
    }
  }
  if (catalog_->galactic_core && !catalog_->galactic_core_discovered) {
    const auto center=view.camera.project(*catalog_->galactic_core,view.viewport_width,view.viewport_height);
    const auto extent=static_cast<float>(catalog_->galactic_core_exclusion_radius*view.camera.pixels_per_world*1.45);
    const UiRect fog{center.x-extent,center.y-extent,extent*2,extent*2};
    if(extent>=1&&intersects(fog,viewport)){out.world.emplace_back(Image{assets_->undisclosed_core_fog(),fog,std::nullopt,{255,255,255,255},viewport});last_stats_.undisclosed_core_fog_images=1;}
  }
}

void NativeGalaxyBackdrop::clear_render_stats() noexcept { last_stats_ = {}; }
GalaxyBackdropRenderStats NativeGalaxyBackdrop::last_render_stats() const noexcept { return last_stats_; }

std::optional<GalaxyBackdropFrame> NativeGalaxyBackdrop::artwork_frame() const { return artwork_frame_; }
Camera NativeGalaxyBackdrop::fit_camera(const int width, const int height) const {
  if (!artwork_frame_) throw std::logic_error("Galaxy backdrop must be bound before fitting its camera.");
  return galaxy_artwork_fit_camera(*artwork_frame_, width, height);
}
std::size_t NativeGalaxyBackdrop::regional_point_count() const noexcept { return regional_points_.size(); }

void promote_legacy_galaxy_foreground(DrawList &out,
                                      const std::size_t insertion_index) {
  if (insertion_index > out.world.size())
    throw std::invalid_argument("Galaxy foreground insertion point is invalid.");
  std::vector<WorldCommand> foreground;
  foreground.reserve(out.lines.size()+out.circles.size());
  for(auto &value:out.lines)foreground.emplace_back(std::move(value));
  for(auto &value:out.circles)foreground.emplace_back(std::move(value));
  out.world.insert(out.world.begin()+static_cast<std::ptrdiff_t>(insertion_index),
                   std::make_move_iterator(foreground.begin()),
                   std::make_move_iterator(foreground.end()));
  for(auto &value:out.text)out.world.emplace_back(std::move(value));
  out.lines.clear();out.circles.clear();out.text.clear();
}

}  // namespace stellar::native_galaxy_ui
