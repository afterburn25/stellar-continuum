#include <stellar/core/system_background.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/engine/foundation.hpp>
#include "system_background_data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <iomanip>
#include <set>
namespace stellar::core {
namespace {
constexpr std::array<std::string_view,10> keys{"cool","dense","bright","medium","old","cluster","sparse","plane","faint","warm"};
const auto& manifest(){static const auto j=nlohmann::json::parse(system_background_manifest);return j;}
}
const std::vector<BackgroundAsset>& background_assets(){static const auto all=[] {std::vector<BackgroundAsset> out;std::set<std::string> ids;for(const auto& a:manifest().at("assets")){const std::string key=a.at("categoryKey");auto found=std::ranges::find(keys,key);if(found==keys.end())throw std::logic_error("Unknown reviewed starfield category");BackgroundAsset x{a.at("id"),a.at("category"),a.at("path"),static_cast<BackgroundCategory>(found-keys.begin()),a.at("density"),a.at("band"),a.at("cluster")};if(!ids.insert(x.id).second)throw std::logic_error("Duplicate reviewed starfield ID");out.push_back(std::move(x));}return out;}();return all;}
std::string_view background_category_name(BackgroundCategory c){static const auto names=manifest().at("categories").get<std::vector<std::string>>();return names.at(static_cast<int>(c));}
const BackgroundAsset& background_asset(std::string_view id){const auto& all=background_assets();auto i=std::ranges::find(all,id,&BackgroundAsset::id);if(i==all.end())throw std::invalid_argument("Unknown or rejected starfield");return *i;}
SystemBackgroundProfile select_system_background(const BackgroundEnvironment& e,std::uint64_t seed,int id,std::size_t rank){
 if(!std::isfinite(e.stellar_density)||e.stellar_density<0||e.stellar_density>1||!std::isfinite(e.x)||!std::isfinite(e.y))throw std::invalid_argument("Invalid background environment");
 SystemBackgroundProfile p;p.system_id=id;p.environment=e;p.seed=seed^(static_cast<std::uint64_t>(id)*0x9e3779b97f4a7c15ULL)^0x534b595631ULL;
 stellar::engine::DeterministicRandom rng(p.seed);
 // Sol's approved distant-star reference is independent of galaxy seed,
 // population and neighborhood rank. Match the canonical preset, never a name.
 // This is a supplied artistic plate, not a claimed observed constellation map.
 if(e.canonical_sol){p.category=BackgroundCategory::Faint;p.asset_id="starfield-map-reference";p.weights[8]=1;p.exposure=1;p.local_nebula=false;return p;}
 // Presentation eligibility is independent of physical stellar density. The
 // galaxy's density/phenomena metadata remains available to simulation and QA;
 // only reviewed faint art is eligible for a readable local sky.
 p.category=BackgroundCategory::Faint;p.weights[8]=1;p.exposure=1;
 const auto& pool=background_assets();
 if(pool.empty())throw std::logic_error("No approved faint system backgrounds");
 const auto offset=static_cast<std::size_t>(rng.unit_double()*pool.size());
 p.asset_id=pool[(offset+rank)%pool.size()].id;
 p.orientation=(rng.unit_double()<.5?0.:std::numbers::pi);
 p.mirror=rng.unit_double()<.5;
 p.crop_x=(rng.unit_double()-.5)*.035;p.crop_y=(rng.unit_double()-.5)*.035;
 return p;
}
void SystemBackgroundCatalog::bind(const FreshCampaignState& world){
 seed_=world.seed;locations_.clear();profiles_.clear();configuration_.reset();phenomena_.reset();population_={};radius_=1;
 if(world.generation_metadata){configuration_=world.generation_metadata->configuration;phenomena_=world.generation_metadata->phenomena;population_=world.generation_metadata->stellar_population.value_or(StellarPopulationOptions{});}
 if(configuration_){population_={configuration_->morphology,configuration_->resolved_population};const auto f=galaxy_footprint_frame(configuration_->morphology,configuration_->system_count,configuration_->resolved_population);radius_=std::max(f.width,f.height)*.5;}
 else for(const auto& s:world.systems)radius_=std::max(radius_,double(std::hypot(s.position.x,s.position.y)));
 cell_size_=std::max(1.,radius_*.065);std::map<std::pair<int,int>,std::vector<int>> cells;
 for(const auto& s:world.systems){locations_[s.id]={s.id,s.position.x,s.position.y,s.stellar_region.value_or(StellarRegion::Disk),0,0,s.catalog_preset_id==sol_catalog_preset_id};cells[{static_cast<int>(std::floor(s.position.x/cell_size_)),static_cast<int>(std::floor(s.position.y/cell_size_))}].push_back(s.id);}
 for(auto& [cell,ids]:cells){std::sort(ids.begin(),ids.end());std::size_t n=0;for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)if(auto i=cells.find({cell.first+x,cell.second+y});i!=cells.end())n+=i->second.size();for(std::size_t i=0;i<ids.size();++i){auto& l=locations_.at(ids[i]);l.rank=i;l.neighbors=n-1;}}
}
const SystemBackgroundProfile& SystemBackgroundCatalog::profile(int id){
 if(auto i=profiles_.find(id);i!=profiles_.end())return i->second;const auto& l=locations_.at(id);BackgroundEnvironment e;e.canonical_sol=l.canonical_sol;e.x=l.x;e.y=l.y;e.region=l.region;e.radius=std::hypot(l.x,l.y);e.radial_fraction=e.radius/radius_;e.morphology=population_.morphology;e.population=population_.state;
 const double expected=std::max(1.,locations_.size()*9*cell_size_*cell_size_/(std::numbers::pi*radius_*radius_));const auto neighbors=std::clamp(l.neighbors/(expected*2.5),0.,1.);
 double footprint=configuration_?phenomenon_footprint_density(*configuration_,l.x,l.y):std::clamp(1-e.radial_fraction,0.,1.);
 e.stellar_density=std::clamp(footprint*.72+neighbors*.28,0.,1.);if(l.region==StellarRegion::Halo)e.stellar_density=std::min(.12,e.stellar_density);
 const auto context=phenomenon_context(phenomena_?&*phenomena_:nullptr,l.x,l.y,id);
 for(const auto& o:context.overlaps){e.phenomena.push_back(o.id);e.gas_density=std::max(e.gas_density,double(o.density));e.nebula_edge|=o.density<.3;const auto& r=*std::ranges::find(phenomena_->regions,o.id,&GalaxyPhenomenon::id);if(phenomenon_definition(r.type).star_forming)e.star_formation=std::max(e.star_formation,double(o.density));if(r.type==PhenomenonType::DarkNebula||r.type==PhenomenonType::MolecularCloud||r.type==PhenomenonType::DustLane)e.dark_optical_depth+=o.density*r.opacity*4;}
 const double activity=std::array{1.,.7,.3,.1,.035}[static_cast<int>(e.population)];
 const bool forming=l.region==StellarRegion::Arm||l.region==StellarRegion::StarForming||l.region==StellarRegion::IrregularClump||l.region==StellarRegion::Ring;
 e.star_formation=std::max(e.star_formation,activity*e.stellar_density*(forming?1:.25));
 if(profiles_.size()>=4096)profiles_.clear();return profiles_.emplace(id,select_system_background(e,seed_,id,l.rank)).first->second;
}
std::vector<std::pair<std::string,int>> SystemBackgroundCatalog::region_examples(){
 std::map<std::string,int> picks;double min=1e30,max=-1;int center=-1,rim=-1;
 for(const auto& [id,l]:locations_){const double r=std::hypot(l.x,l.y);if(r<min){min=r;center=id;}if(r>max){max=r;rim=id;}picks.try_emplace(std::string(stellar_region_name(l.region)),id);
  if(phenomena_&&!picks.contains("Nebula interior")){const auto c=phenomenon_context(&*phenomena_,l.x,l.y,id);if(c.visual_intensity>.2)picks.try_emplace("Nebula interior",id);}
  if(phenomena_&&!picks.contains("Nebula edge")){const auto c=phenomenon_context(&*phenomena_,l.x,l.y,id);if(!c.overlaps.empty()&&c.overlaps.front().density<.3)picks.try_emplace("Nebula edge",id);}}
 if(center>=0)picks["Nearest to center"]=center;if(rim>=0)picks["Outer rim"]=rim;return {picks.begin(),picks.end()};
}
std::string system_background_diagnostics(const SystemBackgroundProfile& p){const auto& e=p.environment;std::ostringstream s;s<<std::fixed<<std::setprecision(3)<<morphology_name(e.morphology)<<" / "<<population_state_name(e.population)<<"\nPosition "<<e.x<<", "<<e.y<<" ly · height: planar catalog\n"<<stellar_region_name(e.region)<<" · radius "<<e.radius<<" ly ("<<e.radial_fraction<<")\nStellar density "<<e.stellar_density<<" · gas "<<e.gas_density<<" · star formation "<<e.star_formation<<"\nPhenomena:";for(auto id:e.phenomena)s<<' '<<id;s<<" · dark depth "<<e.dark_optical_depth<<"\n"<<background_category_name(p.category)<<"\n"<<p.asset_id<<" · blend "<<(p.blend_asset_id.empty()?"none":p.blend_asset_id)<<" / "<<p.blend_strength<<"\nExposure "<<p.exposure<<" · angle "<<p.orientation<<" · mirrored "<<p.mirror<<"\nReviewed faint-sky weights (physical environment retained):\n";for(int i=0;i<10;++i)s<<keys[i]<<' '<<std::setprecision(1)<<p.weights[i]*100<<"%  ";return s.str();}
}
