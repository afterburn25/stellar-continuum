#pragma once
#include "map_camera.hpp"
#include <stellar/core/galaxy_phenomena.hpp>
#include <stellar/core/phenomenon_art.hpp>
#include <stellar/engine/texture_decal.hpp>
#include <stellar/engine/spatial_region_index.hpp>
#include <stellar/engine/native_image_preparation.hpp>
#include <chrono>
#include <map>
#include <set>
namespace stellar::native_phenomena {
using namespace stellar::native_map;
struct VisualOptions {int density{1};int developer_percent{-1};bool bounds{},heatmap{},labels{},membership{},region_bias{},filenames{};bool background_stars{true};double local_opacity{1};bool local_nebula{true};};
double visual_multiplier(const VisualOptions&);
double local_visual_multiplier(const VisualOptions&,double zoom,bool combat=false);
struct PhenomenonAtlasLayout {int tile{},columns{},rows{};};
PhenomenonAtlasLayout phenomenon_atlas_layout(std::size_t count);
std::shared_ptr<const RgbaImage> make_phenomenon_atlas(const stellar::core::GalaxyPhenomena&);
std::shared_ptr<const RgbaImage> make_local_environment(const stellar::core::GalaxyPhenomena&,const stellar::core::SystemPhenomenonContext&,const std::filesystem::path& root=std::filesystem::current_path());
DecalMapping phenomenon_art_mapping(const stellar::core::GalaxyPhenomenon&,const stellar::core::PhenomenonVisualAsset&);
std::vector<stellar::core::PhenomenonOverlap> local_art_overlaps(const stellar::core::GalaxyPhenomena&,const stellar::core::SystemPhenomenonContext&);
class NativePhenomena {
public:
  void use_assets(std::filesystem::path root);
  void use_queue(std::shared_ptr<ImagePreparationQueue> q){queue_=std::move(q);}
  void bind(const stellar::core::GalaxyPhenomena* field);
  void poll();
  void append_map(DrawList&,const Camera&,int,int,const VisualOptions&,const std::set<std::uint32_t>& surveyed);
  void append_system(DrawList&,int system,double x,double y,int w,int h,double zoom,const VisualOptions&,bool combat=false);
  void inspect(DrawList&,const Camera&,Point,int,int,const std::set<std::uint32_t>& surveyed,bool developer,bool pinned=false)const;
  const stellar::core::SystemPhenomenonContext& context(int,double,double);
  const stellar::core::GalaxyPhenomena* field()const{return field_?&*field_:nullptr;}
  bool ready()const{return ready_;}
  std::size_t cache_bytes()const;
private:
  void request_atlas();
  std::shared_ptr<const RgbaImage> request_art(const stellar::core::PhenomenonVisualAsset&,int lod);
  void collect_art();
  struct ArtEntry {std::shared_ptr<const RgbaImage> image;std::optional<ImagePreparationQueue::Ticket> job;std::uint64_t touched{};};
  std::filesystem::path asset_root_{std::filesystem::current_path()};
  std::map<std::pair<std::string,int>,ArtEntry> art_;
  std::uint64_t frame_{};
  stellar::engine::SpatialRegionIndex index_;
  std::optional<stellar::core::GalaxyPhenomena> field_;
  std::shared_ptr<ImagePreparationQueue> queue_;
  std::optional<ImagePreparationQueue::Ticket> atlas_job_,local_job_;
  std::shared_ptr<const RgbaImage> atlas_,local_;
  std::map<int,stellar::core::SystemPhenomenonContext> contexts_;
  int local_system_{-1};bool ready_{true};
  std::chrono::steady_clock::time_point transition_{std::chrono::steady_clock::now()};
  bool previous_system_{};
};
}
