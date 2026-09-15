#include "native_celestial_appearance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace stellar::native_system_ui {
namespace {
using namespace stellar::native_map;
constexpr float pi = 3.14159265358979323846f;
constexpr float tau = 2.f * pi;

float fract(float value) { return value - std::floor(value); }
float hash(float x, float y) {
  return fract(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f);
}
float smooth(float value) { return value * value * (3.f - 2.f * value); }
float smoothstep(float low, float high, float value) {
  return smooth(std::clamp((value - low) / (high - low), 0.f, 1.f));
}
float noise(float x, float y) {
  const float ix = std::floor(x), iy = std::floor(y);
  const float fx = smooth(fract(x)), fy = smooth(fract(y));
  const float a = hash(ix, iy), b = hash(ix + 1, iy);
  const float c = hash(ix, iy + 1), d = hash(ix + 1, iy + 1);
  return std::lerp(std::lerp(a, b, fx), std::lerp(c, d, fx), fy);
}
std::uint8_t byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.f), 0l, 255l));
}
void pixel(std::vector<std::uint8_t> &rgba, int width, int x, int y,
           float r, float g, float b, float a) {
  const auto at = static_cast<std::size_t>((y * width + x) * 4);
  rgba[at] = byte(r); rgba[at + 1] = byte(g); rgba[at + 2] = byte(b);
  rgba[at + 3] = byte(a);
}
std::optional<std::pair<Point,Point>> clip_line(Point a,Point b,UiRect r){
  float first=0,last=1;const float dx=b.x-a.x,dy=b.y-a.y;
  const std::array p{-dx,dx,-dy,dy};
  const std::array q{a.x-r.x,r.x+r.width-a.x,a.y-r.y,r.y+r.height-a.y};
  for(std::size_t i=0;i<p.size();++i){if(p[i]==0){if(q[i]<0)return std::nullopt;continue;}const float value=q[i]/p[i];if(p[i]<0)first=std::max(first,value);else last=std::min(last,value);if(first>last)return std::nullopt;}
  return std::pair{Point{a.x+first*dx,a.y+first*dy},Point{a.x+last*dx,a.y+last*dy}};
}
std::shared_ptr<const RgbaImage> make_star(Color color, bool black_hole,
                                           std::uint32_t seed) {
  constexpr int size = NativeCelestialAppearanceRenderer::stellar_texture_size;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size * size * 4));
  const float cr = color.r / 255.f, cg = color.g / 255.f, cb = color.b / 255.f;
  struct Spot { float x{}, y{}, radius{}, depth{}; };
  std::array<Spot, 4> spots{};
  for (std::size_t i = 0; i < spots.size(); ++i) {
    const float bearing = hash(static_cast<float>(seed) + i * 17.3f, 4.1f) * tau;
    const float distance = .08f + hash(static_cast<float>(seed) + i * 9.7f, 8.3f) * .27f;
    spots[i] = {std::cos(bearing) * distance, std::sin(bearing) * distance,
                .018f + hash(static_cast<float>(seed) + i * 3.1f, 22.7f) * .026f,
                .20f + hash(static_cast<float>(seed) + i * 5.9f, 16.4f) * .25f};
  }
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const float px = (2.f * (x + .5f) / size - 1.f);
    const float py = (2.f * (y + .5f) / size - 1.f);
    const float radial = std::sqrt(px * px + py * py);
    const float theta = std::atan2(py, px);
    if (black_hole) {
      const float horizon = 1.f - std::clamp((radial - .41f) / .025f, 0.f, 1.f);
      const float rim = std::max(0.f, 1.f - std::abs(radial - .455f) / .035f);
      const float disc = std::max(0.f, 1.f - std::abs(std::sqrt(px * px + py * py * 15.f) - .58f) / .13f);
      const float alpha = std::clamp(std::max({horizon, rim * .85f, disc * .34f}), 0.f, 1.f);
      pixel(rgba, size, x, y, .015f + rim * .45f, .02f + rim * .34f,
            .04f + rim * .58f, alpha); continue;
    }
    const float disc_alpha = 1.f - std::clamp((radial - .448f) / .016f, 0.f, 1.f);
    const float corona_distance = std::max(0.f, radial - .455f);
    const float streamers = .75f + .13f * std::sin(theta * 9.f) +
                            .09f * std::sin(theta * 17.f + .6f) +
                            .06f * std::sin(theta * 31.f + std::sin(theta * 5.f));
    const float corona = std::exp(-corona_distance * 10.f) * streamers *
                         (1.f - std::clamp((radial - .68f) / .32f, 0.f, 1.f)) * .42f;
    if (disc_alpha <= 0 && corona <= .002f) continue;
    const float offset=static_cast<float>(seed&1023u)*.031f;
    const float warp_x = (noise(px * 8.f + offset, py * 8.f - offset) - .5f) * .055f;
    const float warp_y = (noise(px * 8.f - offset * .4f, py * 8.f + offset * .6f) - .5f) * .055f;
    const float cells = noise((px + warp_x) * 112.f + 1.7f + offset,
                              (py + warp_y) * 112.f - 2.1f - offset * .7f);
    const float medium = noise((px - warp_y) * 39.f + offset * .3f,
                               (py + warp_x) * 39.f - offset * .2f);
    const float broad = noise(px * 10.f - 3.2f-offset*.2f, py * 10.f + .8f+offset*.3f);
    const float limb = radial < .455f ? std::sqrt(std::max(0.f, 1.f - (radial / .455f) * (radial / .455f))) : 0.f;
    float spot_darkening{};
    for (const auto &spot : spots) {
      const float dx = px - spot.x, dy = py - spot.y;
      const float normalized = (dx * dx + dy * dy) / (spot.radius * spot.radius);
      const float penumbra = std::clamp(1.f - normalized * .35f, 0.f, 1.f);
      const float core = std::clamp(1.f - normalized, 0.f, 1.f);
      spot_darkening = std::max(spot_darkening,
                                penumbra * spot.depth * .45f + core * spot.depth);
    }
    const float granules = (.66f + cells * .20f + medium * .10f + broad * .08f +
                            limb * .14f) * (1.f - spot_darkening);
    const float rim = std::clamp((radial / .455f - .76f) / .22f, 0.f, 1.f) * disc_alpha;
    const float core_r = std::clamp(cr * granules + rim * .22f, 0.f, 1.f);
    const float core_g = std::clamp(cg * granules + rim * .19f, 0.f, 1.f);
    const float core_b = std::clamp(cb * granules + rim * .16f, 0.f, 1.f);
    const float blend = disc_alpha;
    pixel(rgba, size, x, y, std::lerp(cr * .62f, core_r, blend),
          std::lerp(cg * .58f, core_g, blend), std::lerp(cb * .55f, core_b, blend),
          std::max(disc_alpha, corona));
  }
  return RgbaImage::create(size, size, std::move(rgba));
}
std::shared_ptr<const RgbaImage> make_ring(bool front) {
  constexpr int size = NativeCelestialAppearanceRenderer::ring_texture_size;
  constexpr float extent = 2.30f, tilt = -.36f, flattening = .33f;
  const float c = std::cos(tilt), s = std::sin(tilt);
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size * size * 4));
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const float sx = (2.f * (x + .5f) / size - 1.f) * extent;
    const float sy = (2.f * (y + .5f) / size - 1.f) * extent;
    const float local_x = c * sx + s * sy, local_y = -s * sx + c * sy;
    const float radial = std::sqrt(local_x * local_x + (local_y / flattening) * (local_y / flattening));
    const float inner = smoothstep(1.215f, 1.245f, radial);
    const float outer = 1.f - smoothstep(2.225f, 2.255f, radial);
    if (inner <= 0.f || outer <= 0.f) continue;
    const float cassini = 1.f - .94f * (1.f - smoothstep(.018f, .034f,
                                      std::abs(radial - 1.88f)));
    const float broad_bands = .72f + .13f * std::sin(radial * 31.f + .7f) +
                              .10f * std::sin(radial * 67.f - .9f);
    const float fine_structure = .91f + .09f * std::sin(radial * 173.f);
    const float variation = std::clamp(broad_bands * fine_structure, .36f, 1.f);
    const float target_alpha = inner * outer * cassini * variation * .68f;
    // Separate back/front textures are bilinearly sampled. A hard exclusive
    // split makes both samples transparent at the boundary and leaves a dark
    // crack outside the globe. Across this narrow overlap, choose straight
    // alpha values whose back-then-front composition remains target_alpha.
    const float front_weight = smoothstep(-.020f, .020f, local_y);
    const float front_alpha = target_alpha * front_weight;
    const float alpha = front
                            ? front_alpha
                            : (target_alpha - front_alpha) /
                                  std::max(.001f, 1.f - front_alpha);
    pixel(rgba, size, x, y, .78f * variation, .72f * variation,
          .58f * variation, alpha);
  }
  return RgbaImage::create(size, size, std::move(rgba));
}
} // namespace

struct NativeCelestialAppearanceRenderer::Storage {
  struct Key { Color color{}; std::uint32_t seed{}; bool black_hole{}, ring{}, front{}; bool operator==(const Key&other)const noexcept{return color.r==other.color.r&&color.g==other.color.g&&color.b==other.color.b&&color.a==other.color.a&&seed==other.seed&&black_hole==other.black_hole&&ring==other.ring&&front==other.front;} };
  struct Entry { Key key; std::shared_ptr<const RgbaImage> image; std::uint64_t use{}; };
  std::vector<Entry> entries;std::size_t bytes{},generated{};std::uint64_t use{};
  std::shared_ptr<const RgbaImage> obtain(Key key) {
    const auto found=std::ranges::find(entries,key,&Entry::key);
    if(found!=entries.end()){found->use=++use;return found->image;}
    auto image=key.ring?make_ring(key.front):make_star(key.color,key.black_hole,key.seed);
    while(!entries.empty()&&(entries.size()>=NativeCelestialAppearanceRenderer::maximum_cached_resources||bytes+image->byte_size()>NativeCelestialAppearanceRenderer::maximum_cached_bytes)){
      const auto oldest=std::ranges::min_element(entries,{},&Entry::use);bytes-=oldest->image->byte_size();entries.erase(oldest);
    }
    if(image->byte_size()>NativeCelestialAppearanceRenderer::maximum_cached_bytes)throw std::runtime_error("Celestial appearance resource exceeds its cache budget.");
    bytes+=image->byte_size();entries.push_back({key,image,++use});++generated;return image;
  }
};
NativeCelestialAppearanceRenderer::NativeCelestialAppearanceRenderer():storage_(std::make_unique<Storage>()){}
NativeCelestialAppearanceRenderer::~NativeCelestialAppearanceRenderer()=default;
NativeCelestialAppearanceRenderer::NativeCelestialAppearanceRenderer(NativeCelestialAppearanceRenderer&&)noexcept=default;
NativeCelestialAppearanceRenderer&NativeCelestialAppearanceRenderer::operator=(NativeCelestialAppearanceRenderer&&)noexcept=default;
void NativeCelestialAppearanceRenderer::append_stellar_disc(DrawList&out,Point center,float radius,const NativeStellarDiscAppearance&a,double seconds,std::optional<UiRect>clip){
  if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(radius)||radius<=0||!std::isfinite(seconds))throw std::invalid_argument("Stellar appearance geometry must be finite and positive.");
  const auto image=storage_->obtain({a.spectral_color,a.deterministic_seed,a.black_hole,false,false});const float extent=radius*2.20f;
  out.world.emplace_back(Image{image,{center.x-extent,center.y-extent,extent*2,extent*2},std::nullopt,{255,255,255,255},clip});
  if(a.black_hole)return;
  const float seed=static_cast<float>(a.deterministic_seed&65535u);
  const float slot_length=9.5f+hash(seed,4.7f)*3.5f;
  const float shifted=static_cast<float>(seconds)+hash(seed,17.1f)*slot_length;
  const float epoch=std::floor(shifted/slot_length),phase=fract(shifted/slot_length);
  const float roll=hash(seed*2.31f+epoch,31.7f),previous=hash(seed*2.31f+epoch-1.f,31.7f);
  const bool enabled=roll>=.48f||previous<.48f;
  const float start=.08f+hash(seed+epoch*1.17f,8.3f)*.12f;
  const float duration=.29f+hash(seed*3.07f,epoch+12.4f)*.20f;
  const float life=std::clamp((phase-start)/duration,0.f,1.f);
  const auto smoothstep=[](float low,float high,float value){const float u=std::clamp((value-low)/(high-low),0.f,1.f);return u*u*(3.f-2.f*u);};
  const float envelope=enabled?smoothstep(0,.28f,life)*(1.f-smoothstep(.58f,1.f,life)):0.f;
  if(envelope>0){const float bearing=(hash(epoch*3.17f+seed,19.3f)-.5f)*tau;const float half_width=.12f+.12f*hash(epoch+4.2f,seed);const float height=.10f+.17f*hash(seed*1.9f,epoch+7.6f);for(int strand=0;strand<2;++strand){Point prior{};for(int segment=0;segment<=18;++segment){const float u=segment/18.f,signed_angle=(u*2.f-1.f)*half_width;const float arch=std::sqrt(std::max(0.f,1.f-(signed_angle/half_width)*(signed_angle/half_width)));const float irregular=(noise(signed_angle*24.f+epoch,static_cast<float>(seconds)*.075f+seed)-.5f)*.032f;const float r=radius*(.96f+arch*height+irregular+strand*.025f);const float theta=bearing+signed_angle;Point next{center.x+std::cos(theta)*r,center.y+std::sin(theta)*r};if(segment){const auto visible=clip?clip_line(prior,next,*clip):std::optional<std::pair<Point,Point>>{{prior,next}};if(visible)out.world.emplace_back(Line{visible->first,visible->second,{a.spectral_color.r,a.spectral_color.g,a.spectral_color.b,byte(envelope*(strand?.34f:.72f))}});}prior=next;}}}
}
void NativeCelestialAppearanceRenderer::append_ring_back(DrawList&out,Point center,float radius,std::optional<UiRect>clip){if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(radius)||radius<=0)throw std::invalid_argument("Ring geometry must be finite and positive.");const float e=radius*2.30f;out.world.emplace_back(Image{storage_->obtain({{},0,false,true,false}),{center.x-e,center.y-e,e*2,e*2},std::nullopt,{255,255,255,255},clip});}
void NativeCelestialAppearanceRenderer::append_ring_front(DrawList&out,Point center,float radius,std::optional<UiRect>clip){if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(radius)||radius<=0)throw std::invalid_argument("Ring geometry must be finite and positive.");const float e=radius*2.30f;out.world.emplace_back(Image{storage_->obtain({{},0,false,true,true}),{center.x-e,center.y-e,e*2,e*2},std::nullopt,{255,255,255,255},clip});}
NativeCelestialAppearanceStats NativeCelestialAppearanceRenderer::stats()const noexcept{return {storage_->entries.size(),storage_->bytes,storage_->generated};}
void NativeCelestialAppearanceRenderer::clear()noexcept{storage_->entries.clear();storage_->bytes=0;}
} // namespace stellar::native_system_ui
