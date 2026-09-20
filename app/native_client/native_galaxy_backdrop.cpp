#include "native_galaxy_backdrop.hpp"
#include "native_starfield_style.hpp"
#include <stellar/core/phenomenon_art.hpp>
#include <stellar/engine/texture_decal.hpp>
#include <stellar/engine/texture_coverage_mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace stellar::native_galaxy_ui {
namespace {
using namespace stellar::native_map;

// The approved spiral is a wide, inclined disc. Fit its visible oval, rather
// than stretching the source into the old square image's coordinate frame.
constexpr double galaxy_art_aspect = 1456. / 816.;
constexpr double vertical_disc_ratio = .54;
constexpr double visible_disc_half_width = .80;
constexpr double image_core_y = .47;

[[nodiscard]] std::string utf8(const std::filesystem::path &path) {
  const auto encoded = path.u8string();
  return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

[[nodiscard]] bool finite_point(const WorldPoint point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] UiRect cover(const RgbaImage &image, int width, int height) {
  // One pixel of overscan prevents rounding/filtering seams at any aspect ratio.
  const auto scale = std::max(static_cast<float>(width+2) / image.width(),
                              static_cast<float>(height+2) / image.height());
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

[[nodiscard]] std::shared_ptr<const RgbaImage> make_core_fog(const std::filesystem::path& root) {
  const auto& asset=stellar::core::phenomenon_art_role("undisclosed_core_fog");
  auto image=prepare_decal_texture(*decode_rgba_image(root/asset.path),1024,DecalBlendProfile::Luminous);
  auto pixels=image->pixels();for(std::size_t i=3;i<pixels.size();i+=4)pixels[i]=static_cast<std::uint8_t>(pixels[i]*.68);
  return RgbaImage::create(image->width(),image->height(),std::move(pixels));
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
  const auto width = std::max(1., required * 2. * 1.04 / visible_disc_half_width);
  const auto height = width / galaxy_art_aspect;
  return {center.x - width * .5, center.y - height * image_core_y, width, height};
}

Camera galaxy_artwork_fit_camera(const GalaxyBackdropFrame &frame,
                                 const int viewport_width,
                                 const int viewport_height,
                                 const double fill_fraction,
                                 const std::optional<UiRect> content_region) {
  if (viewport_width <= 0 || viewport_height <= 0 ||
      !std::isfinite(frame.left) || !std::isfinite(frame.top) ||
      !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
      frame.width <= 0. || frame.height <= 0. ||
      !std::isfinite(fill_fraction) || fill_fraction <= 0. || fill_fraction > 1.)
    throw std::invalid_argument("Galaxy artwork fit geometry is invalid.");
  Camera result;
  const auto region=content_region.value_or(UiRect{0,0,static_cast<float>(viewport_width),static_cast<float>(viewport_height)});
  if(!std::isfinite(region.x)||!std::isfinite(region.y)||!std::isfinite(region.width)||!std::isfinite(region.height)||region.x<0||region.y<0||region.width<=0||region.height<=0||region.x+region.width>viewport_width||region.y+region.height>viewport_height)
    throw std::invalid_argument("Galaxy artwork content region is invalid.");
  result.center = {frame.left + frame.width * .5,
                   frame.top + frame.height * .5};
  result.pixels_per_world =
      std::min(static_cast<double>(region.width) / frame.width,
               static_cast<double>(region.height) / frame.height) *
      fill_fraction;
  result.center.x+=(viewport_width*.5-region.x-region.width*.5)/result.pixels_per_world;
  result.center.y+=(viewport_height*.5-region.y-region.height*.5)/result.pixels_per_world;
  return result;
}

NativeGalaxyBackdropAssets::NativeGalaxyBackdropAssets(std::filesystem::path root)
    : asset_root_(std::move(root)) {
  if (asset_root_.empty()) throw std::invalid_argument("Galaxy artwork asset root is required.");
  asset_root_=std::filesystem::absolute(std::move(asset_root_));
}

void NativeGalaxyBackdropAssets::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Galaxy backdrop assets must be used on their owner thread.");
}

std::shared_ptr<const RgbaImage> &NativeGalaxyBackdropAssets::slot(
    const ArtworkKind kind) {
  switch (kind) {
    case ArtworkKind::deep_field: return deep_field_;
    case ArtworkKind::star_background: return star_background_;
    case ArtworkKind::galaxy_layer: return galaxy_layer_;
    case ArtworkKind::regional_nebula: return regional_nebula_;
  }
  throw std::logic_error("Galaxy artwork kind is invalid.");
}

NativeGalaxyBackdropAssets::ArtworkSource NativeGalaxyBackdropAssets::source(
    const ArtworkKind kind) const {
  switch (kind) {
    case ArtworkKind::deep_field:
      return {asset_root_ / "assets/visual/space/deep-field-v3.png", "Galaxy deep field",
              maximum_deep_field_bytes};
    case ArtworkKind::star_background:
      return {asset_root_ / "assets/visual/space/star-background.png", "Regional star background",
              maximum_deep_field_bytes};
    case ArtworkKind::galaxy_layer:
      return {asset_root_ / galaxy_layer_path_, "Galaxy layer"};
    case ArtworkKind::regional_nebula:
      return {asset_root_ / "assets/visual/space/regional-nebula-b.png", "Regional nebula"};
  }
  throw std::logic_error("Galaxy artwork kind is invalid.");
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::decode_source(
    const ArtworkSource &source) {
  try {
    auto image = decode_rgba_image(source.path,0,ImageDecodeUsage::PixelsOnly);
    if (image->byte_size() > source.maximum_bytes)
      throw std::length_error("Galaxy artwork exceeds the prepared image byte limit.");
    if (source.path.filename() == "spiral-galaxy-v3.png" || source.path.filename().string().find("gas_dust_only")!=std::string::npos) {
      // The supplied RGB artwork has a near-black matte. Convert that matte
      // into straight alpha during background preparation, retaining luminous
      // arm detail while letting the deep field show through empty space.
      // Keep the source PNG byte-for-byte unchanged in the asset manifest.
      auto pixels = image->pixels();
      for (int y = 0; y < image->height(); ++y) {
        for (int x = 0; x < image->width(); ++x) {
          const auto i = (static_cast<std::size_t>(y) * image->width() + x) * 4u;
          const auto peak = std::max({pixels[i], pixels[i + 1], pixels[i + 2]});
          const auto light = std::max(0, static_cast<int>(peak) - 8);
          const auto edge = std::clamp(std::min({x, y, image->width() - 1 - x,
                                               image->height() - 1 - y}) / 16.f, 0.f, 1.f);
          for (std::size_t c = 0; c < 3; ++c)
            pixels[i + c] = light == 0 ? 0 : static_cast<std::uint8_t>(
                std::max(0, static_cast<int>(pixels[i + c]) - 8) * 255 / light);
          pixels[i + 3] = static_cast<std::uint8_t>(std::lround(
              light / 247.f * pixels[i + 3] * edge));
        }
      }
      return RgbaImage::create(image->width(), image->height(), std::move(pixels));
    }
    return image;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(source.label) + " failed to decode: " +
                             utf8(source.path) + ": " + error.what());
  }
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::synchronous(
    const ArtworkKind kind) {
  require_owner();
  auto &result = slot(kind);
  if (result) return result;
  if (const auto pending = std::ranges::find(pending_, kind, &Pending::kind);
      pending != pending_.end())
    pending_.erase(pending);
  auto image = decode_source(source(kind));
  result = std::move(image);
  ++decoded_;
  return result;
}

void NativeGalaxyBackdropAssets::collect_ready() {
  for (std::size_t index{}; index < pending_.size();) {
    if (!pending_[index].ticket.ready()) {
      ++index;
      continue;
    }
    const auto kind = pending_[index].kind;
    auto ticket = std::move(pending_[index].ticket);
    pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
    auto image = ticket.take();
    if (image->byte_size() > source(kind).maximum_bytes)
      throw std::length_error("Galaxy artwork exceeds the prepared image byte limit.");
    auto &result = slot(kind);
    if (!result) {
      result = std::move(image);
      ++decoded_;
    }
  }
}

void NativeGalaxyBackdropAssets::use_background_preparation(
    std::shared_ptr<ImagePreparationQueue> queue) {
  require_owner();
  cancel_preparation();
  preparation_ = std::move(queue);
}

void NativeGalaxyBackdropAssets::set_galaxy_layer_path(std::string path){
  require_owner();if(path.empty())path="assets/visual/space/spiral-galaxy-v3.png";
  if(path!=galaxy_layer_path_){cancel_preparation();galaxy_layer_.reset();galaxy_layer_path_=std::move(path);}
}
void NativeGalaxyBackdropAssets::cancel_preparation() noexcept { pending_.clear();core_fog_job_.reset(); }
std::size_t NativeGalaxyBackdropAssets::pending_count() const noexcept { return pending_.size()+(core_fog_job_?1u:0u); }

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::request(
    const ArtworkKind kind) {
  require_owner();
  collect_ready();
  if (auto &result = slot(kind); result) return result;
  if (!preparation_) return synchronous(kind);
  if (std::ranges::find(pending_, kind, &Pending::kind) != pending_.end()) return {};
  const auto artwork = source(kind);
  auto ticket = preparation_->submit(artwork.maximum_bytes,
      [artwork] { return decode_source(artwork); });
  if (!ticket) return {};
  pending_.push_back({kind, std::move(*ticket)});
  return {};
}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::deep_field(){return synchronous(ArtworkKind::deep_field);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::star_background(){return synchronous(ArtworkKind::star_background);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::galaxy_layer(){return synchronous(ArtworkKind::galaxy_layer);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::regional_nebula(){return synchronous(ArtworkKind::regional_nebula);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::request_deep_field(){return request(ArtworkKind::deep_field);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::request_star_background(){return request(ArtworkKind::star_background);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::request_galaxy_layer(){return request(ArtworkKind::galaxy_layer);}
std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::request_regional_nebula(){return request(ArtworkKind::regional_nebula);}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::undisclosed_core_fog() {
  require_owner();
  if (!core_fog_) {core_fog_job_.reset();core_fog_ = make_core_fog(asset_root_);}
  return core_fog_;
}

std::shared_ptr<const RgbaImage> NativeGalaxyBackdropAssets::request_undisclosed_core_fog() {
  require_owner();
  if(core_fog_)return core_fog_;
  if(!preparation_)return undisclosed_core_fog();
  if(core_fog_job_&&core_fog_job_->ready()){
    core_fog_=core_fog_job_->take();core_fog_job_.reset();return core_fog_;
  }
  if(!core_fog_job_)core_fog_job_=preparation_->submit(4u*1024u*1024u,[root=asset_root_]{return make_core_fog(root);});
  return {};
}

std::size_t NativeGalaxyBackdropAssets::decoded_count() const noexcept { return decoded_; }

NativeGalaxyBackdrop::NativeGalaxyBackdrop(NativeGalaxyBackdropAssets &assets) : assets_(&assets) {}

void NativeGalaxyBackdrop::bind(GalaxyBackdropCatalog catalog) {
  if (catalog_ && catalog.campaign_generation < catalog_->campaign_generation)
    throw std::invalid_argument("A stale campaign cannot replace the galaxy backdrop.");
  auto frame = galaxy_artwork_world_frame(catalog.system_positions, catalog.galactic_core,
                                          catalog.galactic_core_exclusion_radius);
  if(catalog.fixed_artwork_frame)frame=*catalog.fixed_artwork_frame;
  assets_->set_galaxy_layer_path(catalog.map_asset_path);
  regional_points_.clear(); // The supplied stationary star field replaces decorative parallax stars.
  density_layer_.reset();
  if(!catalog.use_spiral_artwork&&catalog.map_asset_path.empty()){
    // Non-spiral morphologies use the generated stellar density itself, so a
    // ring/ellipsoid/clumped population never sits on unrelated spiral arms.
    constexpr int w=512,h=384;
    std::vector<float> density(w*h);
    for(const auto& point:catalog.system_positions){
      const auto x=static_cast<float>((point.x-frame.left)/frame.width*w);
      const auto y=static_cast<float>((point.y-frame.top)/frame.height*h);
      for(int py=std::max(0,static_cast<int>(y)-24);py<std::min(h,static_cast<int>(y)+25);++py)
        for(int px=std::max(0,static_cast<int>(x)-24);px<std::min(w,static_cast<int>(x)+25);++px){
          const float dx=px-x,dy=py-y;
          density[py*w+px]+=std::exp(-(dx*dx+dy*dy)/150.f);
        }
    }
    const auto peak=*std::max_element(density.begin(),density.end());
    std::vector<std::uint8_t> pixels(w*h*4);
    for(int i=0;i<w*h;++i){const float value=peak>0?std::pow(density[i]/peak,.65f):0.f;
      pixels[i*4]=145;pixels[i*4+1]=174;pixels[i*4+2]=198;pixels[i*4+3]=static_cast<std::uint8_t>(std::clamp(value*165.f,0.f,165.f));
    }
    density_layer_=RgbaImage::create(w,h,std::move(pixels));
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

void NativeGalaxyBackdrop::discard_campaign() noexcept { assets_->cancel_preparation();catalog_.reset(); artwork_frame_.reset(); regional_points_.clear();last_stats_={};artwork_ready_=true; }

void NativeGalaxyBackdrop::append(DrawList &out, const GalaxyBackdropView &view) {
  last_stats_ = {};
  if (!view.galaxy_view) {artwork_ready_=true;return;}
  if (!catalog_ || !artwork_frame_ || view.campaign_generation != catalog_->campaign_generation)
    throw std::invalid_argument("Galaxy backdrop view does not match its bound campaign.");
  if (view.viewport_width <= 0 || view.viewport_height <= 0)
    throw std::invalid_argument("Galaxy backdrop viewport must be positive.");
  const auto blend=galaxy_overview_blend(view.camera.pixels_per_world,view.fitted_pixels_per_world);
  artwork_ready_=true;
  last_stats_.overview_blend=blend;
  const UiRect viewport{0,0,static_cast<float>(view.viewport_width),static_cast<float>(view.viewport_height)};
  // The supplied stars belong to the regional map. Their screen-space rectangle
  // is independent of the camera; only the existing overview transition fades it.
  if (blend < .998) {
    auto stars=assets_->request_star_background();
    if (stars) {
      auto tint=faint_starfield_tint;tint.a=static_cast<std::uint8_t>(std::lround(255.*(1.-blend)));
      out.world.emplace_back(Image{stars,cover(*stars,view.viewport_width,view.viewport_height),std::nullopt,tint,viewport});
      last_stats_.star_background_images=1;
    } else artwork_ready_=false;
  }
  if (blend > .002) {
    auto deep=assets_->request_deep_field();
    if (deep) {
      out.world.emplace_back(Image{deep,cover(*deep,view.viewport_width,view.viewport_height),std::nullopt,{255,255,255,static_cast<std::uint8_t>(std::lround(255.*.58*blend))},viewport});
      last_stats_.deep_field_images=1;
    } else artwork_ready_=false;
    const auto &frame=*artwork_frame_;
    const auto top_left=view.camera.project({frame.left,frame.top},view.viewport_width,view.viewport_height);
    const auto destination=UiRect{top_left.x,top_left.y,static_cast<float>(frame.width*view.camera.pixels_per_world),static_cast<float>(frame.height*view.camera.pixels_per_world)};
    if (intersects(destination,viewport)) {
      if (auto galaxy=density_layer_?density_layer_:assets_->request_galaxy_layer()) {
        out.world.emplace_back(Image{galaxy,destination,std::nullopt,{255,255,255,static_cast<std::uint8_t>(std::lround(255.*blend))},viewport});
        last_stats_.galaxy_layer_images=1;
      } else artwork_ready_=false;
    }
  }
  const auto regional=std::clamp(1.-blend*2.,0.,1.);
  last_stats_.regional_opacity=regional;
  if (regional>.002) {
    if(!catalog_->generated_phenomena){
    auto nebula=assets_->request_regional_nebula();
    if (nebula) {
      out.world.emplace_back(Image{nebula,
      regional_cover(*nebula,view.viewport_width,view.viewport_height,view.camera),
      std::nullopt,{255,255,255,static_cast<std::uint8_t>(std::lround(255.*.56*regional))},viewport});
      last_stats_.regional_nebula_images=1;
    } else artwork_ready_=false;
    }
  }
  if (catalog_->galactic_core && !catalog_->galactic_core_discovered) {
    const auto center=view.camera.project(*catalog_->galactic_core,view.viewport_width,view.viewport_height);
    const auto extent=static_cast<float>(catalog_->galactic_core_exclusion_radius*view.camera.pixels_per_world*1.45);
    const UiRect fog{center.x-extent,center.y-extent,extent*2,extent*2};
    if(extent>=1&&intersects(fog,viewport)){
      if(const auto material=assets_->request_undisclosed_core_fog()){
      const double aspect=material->width()/static_cast<double>(material->height());
      out.world.emplace_back(masked_decal_mesh(material,fog,viewport,{255,255,255,190},[&](Point pixel){
        const double radius=std::hypot((pixel.x-center.x)/extent,(pixel.y-center.y)/extent);
        double edge=std::clamp((.96-radius)/.34,0.,1.);return edge*edge*(3.-2.*edge);
      },[&](Point pixel){return Point{.5f+(pixel.x-center.x)/static_cast<float>(2*extent*aspect),.5f+(pixel.y-center.y)/(2*extent)};}));last_stats_.undisclosed_core_fog_images=1;
      }else artwork_ready_=false;
    }
  }
}

void NativeGalaxyBackdrop::clear_render_stats() noexcept { last_stats_ = {};artwork_ready_=true; }
GalaxyBackdropRenderStats NativeGalaxyBackdrop::last_render_stats() const noexcept { return last_stats_; }

std::optional<GalaxyBackdropFrame> NativeGalaxyBackdrop::artwork_frame() const { return artwork_frame_; }
Camera NativeGalaxyBackdrop::fit_camera(const int width, const int height,const std::optional<UiRect> content_region) const {
  if (!artwork_frame_) throw std::logic_error("Galaxy backdrop must be bound before fitting its camera.");
  return galaxy_artwork_fit_camera(*artwork_frame_, width, height,.88,content_region);
}
std::size_t NativeGalaxyBackdrop::regional_point_count() const noexcept { return regional_points_.size(); }
bool NativeGalaxyBackdrop::artwork_ready()const noexcept{return artwork_ready_;}

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
