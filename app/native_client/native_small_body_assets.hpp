#pragma once
#include <stellar/core/small_body_fields.hpp>
#include <stellar/engine/native_image_preparation.hpp>
#include <stellar/engine/texture_decal.hpp>
#include <array>

namespace stellar::native_system_ui {
class NativeSmallBodyAssets {
public:
  void configure(std::filesystem::path root,std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue){root_=std::move(root);queue_=std::move(queue);}
  std::shared_ptr<const stellar::native_map::RgbaImage> image(stellar::core::SmallBodyAssetPool pool,int variant){
    using namespace stellar::native_map;
    if(variant<1||variant>4||static_cast<unsigned>(pool)>=9)throw std::invalid_argument("Invalid small-body artwork identity");
    const auto index=static_cast<std::size_t>(pool)*4+variant-1;
    if(pending_[index]&&pending_[index]->ready()){images_[index]=pending_[index]->take();pending_[index].reset();}
    if(images_[index]||pending_[index]||root_.empty()||!queue_)return images_[index];
    const auto path=root_/"assets/visual/small-bodies"/(std::string(stellar::core::small_body_asset_name(pool))+" "+std::to_string(variant)+".png");
    const bool isolated=pool==stellar::core::SmallBodyAssetPool::RockyBody||pool==stellar::core::SmallBodyAssetPool::IcyBody;
    const int width=isolated?768:512;
    pending_[index]=queue_->submit(static_cast<std::size_t>(width)*width*4,[path,width,isolated]{return prepare_decal_texture(*decode_rgba_image(path),width,isolated?DecalBlendProfile::OpaqueCutout:DecalBlendProfile::Luminous);});
    return {};
  }
  std::size_t loaded_count()const noexcept{std::size_t n=0;for(const auto& p:images_)n+=bool(p);return n;}
private:
  std::filesystem::path root_;
  std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue_;
  std::array<std::shared_ptr<const stellar::native_map::RgbaImage>,36> images_;
  std::array<std::optional<stellar::native_map::ImagePreparationQueue::Ticket>,36> pending_;
};
}
