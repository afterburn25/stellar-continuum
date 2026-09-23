#pragma once
#include <stellar/engine/native_image_preparation.hpp>
#include <stellar/engine/emissive_image.hpp>
#include <filesystem>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace stellar::native_stellar {
// A rim outside the photosphere leaves the supplied surface colors untouched.
void append_selection(stellar::native_map::DrawList&,stellar::native_map::Point,float radius);
struct Asset {
  std::string id,close_asset,distance_asset,fallback;
  stellar::native_map::Color color;
  float luminosity{1},scale{1};
};
class Artwork final {
public:
  explicit Artwork(std::filesystem::path root);
  void use_queue(std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue){queue_=std::move(queue);}
  void begin_frame(){++frame_;std::erase_if(admitted_close_,[&](const auto& item){return item.second+1<frame_;});visible_close_.clear();frame_budgeted_=true;}
  // Only observer-approved identities enter this presentation boundary.
  void append(stellar::native_map::DrawList&,stellar::native_map::Point,float radius,
      const std::string& observed_id,double seconds,std::optional<stellar::native_map::UiRect> clip={},
      std::optional<std::uint64_t> presentation_key={});
  [[nodiscard]] const std::map<std::string,Asset>& manifest()const{return assets_;}
  [[nodiscard]] std::size_t close_count()const{return close_.size();}
  [[nodiscard]] std::size_t distance_count()const{return distance_.size();}
  [[nodiscard]] std::size_t pending_count()const{return pending_.size();}
  [[nodiscard]] std::size_t transition_count()const{return transitions_.size();}
  // Accessibility: holds the polar-pulse emissive at its mean luminance
  // instead of oscillating brightness at ~2.4Hz.
  void set_reduce_flashing(bool on){reduce_flashing_=on;}
  [[nodiscard]] bool reduce_flashing()const{return reduce_flashing_;}
  [[nodiscard]] std::optional<stellar::native_map::EmissiveDisc> photosphere(const std::string& id,stellar::native_map::Point center,float radius)const;
  static constexpr std::size_t maximum_close_images=4,maximum_pending=2;
  static constexpr std::size_t maximum_transitions=1024;
private:
  struct Cached{
    std::shared_ptr<const stellar::native_map::RgbaImage> image;
    std::uint64_t used{};
    std::optional<double> first_visible;
    std::optional<stellar::native_map::EmissiveDisc> disc;
  };
  struct Pending{std::string key;bool close{};stellar::native_map::ImagePreparationQueue::Ticket ticket;};
  struct Transition{double seconds{};float weight{};std::uint64_t used{};};
  std::shared_ptr<const stellar::native_map::RgbaImage> request(const Asset&,bool close);
  static std::shared_ptr<const stellar::native_map::RgbaImage> prepare(const std::filesystem::path&,Asset,bool close);
  void collect();
  std::filesystem::path root_;
  std::map<std::string,Asset> assets_;
  std::map<std::string,Cached> close_,distance_;
  std::vector<Pending> pending_;
  std::vector<std::string> visible_close_;
  std::map<std::string,std::uint64_t> admitted_close_;
  std::uint64_t frame_{};
  // Per visible object, not per texture: two red giants can occupy different
  // detail levels while sharing their immutable artwork and upload budget.
  std::map<std::pair<std::string,std::uint64_t>,Transition> transitions_;
  bool frame_budgeted_{};
  bool reduce_flashing_{};
  std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue_;
  std::uint64_t use_{};
};
}
