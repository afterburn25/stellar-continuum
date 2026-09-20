#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::native_map {
// Crop an axis-aligned projected image before handing it to the bounded
// renderer. Preserve its texture mapping even at large map zoom factors.
[[nodiscard]] inline std::optional<Image> clip_image_to_viewport(Image image,UiRect viewport) {
  const auto valid=[](UiRect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&
    std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0;};
  if(!image.resource||image.rotation_degrees!=0||!valid(image.destination)||!valid(viewport))
    throw std::invalid_argument("Image clipping requires finite unrotated bounds and a resource.");
  const auto d=image.destination;
  const auto s=image.source.value_or(UiRect{0,0,static_cast<float>(image.resource->width()),static_cast<float>(image.resource->height())});
  if(!valid(s)||s.x<0||s.y<0||double(s.x)+s.width>image.resource->width()||double(s.y)+s.height>image.resource->height())
    throw std::invalid_argument("Image clipping source exceeds the resource.");
  double left=std::max(double(d.x),double(viewport.x)),top=std::max(double(d.y),double(viewport.y));
  double right=std::min(double(d.x)+d.width,double(viewport.x)+viewport.width),bottom=std::min(double(d.y)+d.height,double(viewport.y)+viewport.height);
  if(image.clip){const auto c=*image.clip;
    if(!valid(c))throw std::invalid_argument("Invalid image clip bounds.");
    left=std::max(left,double(c.x));top=std::max(top,double(c.y));
    right=std::min(right,double(c.x)+c.width);bottom=std::min(bottom,double(c.y)+c.height);
  }
  if(right<=left||bottom<=top)return std::nullopt;
  const float sx=static_cast<float>(s.x+(left-d.x)/d.width*s.width),sy=static_cast<float>(s.y+(top-d.y)/d.height*s.height);
  const float ex=std::min(static_cast<float>(s.x+(right-d.x)/d.width*s.width),static_cast<float>(image.resource->width()));
  const float ey=std::min(static_cast<float>(s.y+(bottom-d.y)/d.height*s.height),static_cast<float>(image.resource->height()));
  if(ex<=sx||ey<=sy)return std::nullopt;
  image.source=UiRect{sx,sy,ex-sx,ey-sy};
  image.destination={static_cast<float>(left),static_cast<float>(top),static_cast<float>(right-left),static_cast<float>(bottom-top)};
  image.clip=viewport;return image;
}
} // namespace stellar::native_map
