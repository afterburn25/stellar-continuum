#pragma once
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/engine/native_image_preparation.hpp>
#include <stellar/engine/native_scene3d.hpp>
#include <map>
namespace stellar::native_stellar {
struct EruptionDrawRecord {std::uint64_t id{};int variant{},stage{};double progress{};};
class EruptionArtwork final {
public:
 explicit EruptionArtwork(std::filesystem::path root);
 void use_queue(std::shared_ptr<stellar::native_map::ImagePreparationQueue> q){queue_=std::move(q);}
 void begin_frame(std::uint64_t generation,double simulation_day,double seconds,bool running,int quality);
 void append(stellar::native_map::DrawList&,stellar::native_map::Point,float radius,const stellar::core::StellarSystem&,int component,stellar::native_map::UiRect clip);
 const std::vector<EruptionDrawRecord>& records()const{return records_;}
 std::size_t pending_count()const{return pending_.size();}
 std::size_t resident_count()const{return images_.size();}
 static bool detailed_lod(float radius){return radius>=22;}
private:
 struct Playback{double seconds{},fraction{},start{};std::uint64_t used{};};
 struct Cached{std::shared_ptr<const stellar::native_map::RgbaImage> image;std::uint64_t used{};};
 struct Observation{double day{},seconds{};std::uint64_t used{};};
 struct Pending{std::string path;stellar::native_map::ImagePreparationQueue::Ticket ticket;};
 std::shared_ptr<const stellar::native_map::RgbaImage> request(const std::string&);
 void collect();
 std::filesystem::path root_;
 std::map<std::tuple<int,int,int>,std::array<std::string,4>> sequences_;
 std::map<std::string,Cached> images_;std::vector<Pending> pending_;
 std::map<std::uint64_t,Playback> playback_;
 std::map<std::pair<int,int>,Observation> observations_;
 std::vector<EruptionDrawRecord> records_;
 std::array<std::shared_ptr<const stellar::native_map::Mesh3D>,5> meshes_;
 std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue_;
 std::uint64_t generation_{},serial_{};double day_{},seconds_{};bool running_{};int quality_{2},drawn_{};
};
}
