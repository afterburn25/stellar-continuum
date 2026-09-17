#pragma once
#include <stellar/engine/native_image_preparation.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace stellar::native_stellar {
struct Asset {
  std::string id,close_asset,distance_asset,fallback;
  stellar::native_map::Color color;
  float luminosity{1},scale{1};
};
class Artwork final {
public:
  explicit Artwork(std::filesystem::path root);
  void use_queue(std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue){queue_=std::move(queue);}
  void begin_frame(){visible_close_.clear();frame_budgeted_=true;}
  // Only observer-approved identities enter this presentation boundary.
  void append(stellar::native_map::DrawList&,stellar::native_map::Point,float radius,
      const std::string& observed_id,double seconds,std::optional<stellar::native_map::UiRect> clip={});
  [[nodiscard]] const std::map<std::string,Asset>& manifest()const{return assets_;}
  [[nodiscard]] std::size_t close_count()const{return close_.size();}
  [[nodiscard]] std::size_t distance_count()const{return distance_.size();}
  [[nodiscard]] std::size_t pending_count()const{return pending_.size();}
  static constexpr std::size_t maximum_close_images=4,maximum_pending=2;
private:
  struct Cached{
    std::shared_ptr<const stellar::native_map::RgbaImage> image;
    std::uint64_t used{};
    std::optional<double> first_visible;
  };
  struct Pending{std::string key;bool close{};stellar::native_map::ImagePreparationQueue::Ticket ticket;};
  std::shared_ptr<const stellar::native_map::RgbaImage> request(const Asset&,bool close);
  static std::shared_ptr<const stellar::native_map::RgbaImage> prepare(const std::filesystem::path&,Asset,bool close);
  void collect();
  std::filesystem::path root_;
  std::map<std::string,Asset> assets_;
  std::map<std::string,Cached> close_,distance_;
  std::vector<Pending> pending_;
  std::vector<std::string> visible_close_;
  bool frame_budgeted_{};
  std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue_;
  std::uint64_t use_{};
};
}
