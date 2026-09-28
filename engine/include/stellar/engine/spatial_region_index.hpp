#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <vector>

namespace stellar::engine {
struct SpatialBounds {double left{},top{},right{},bottom{};};
// Immutable broad phase. Payloads are caller-owned stable vector indices.
// Ordered cells/results preserve deterministic drawing and query order.
class SpatialRegionIndex {
public:
  explicit SpatialRegionIndex(double cell_size=32):cell_size_(cell_size){if(!std::isfinite(cell_size)||cell_size<=0)throw std::invalid_argument("Invalid spatial cell size");}
  void clear(){cells_.clear();bounds_.clear();}
  void insert(std::size_t id,SpatialBounds bounds){
    if(!std::isfinite(bounds.left)||!std::isfinite(bounds.top)||!std::isfinite(bounds.right)||!std::isfinite(bounds.bottom)||bounds.right<bounds.left||bounds.bottom<bounds.top||std::max({std::abs(bounds.left),std::abs(bounds.top),std::abs(bounds.right),std::abs(bounds.bottom)})>1e7)throw std::invalid_argument("Invalid spatial bounds");
    const int left=cell(bounds.left),right=cell(bounds.right),top=cell(bounds.top),bottom=cell(bounds.bottom);
    if(static_cast<double>(right-left+1)*(bottom-top+1)>65536)throw std::length_error("Spatial region covers too many cells");
    bounds_[id]=bounds;for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)cells_[{x,y}].push_back(id);
  }
  std::vector<std::size_t> query(SpatialBounds area)const {
    std::vector<std::size_t> result;
    if(!std::isfinite(area.left)||!std::isfinite(area.top)||!std::isfinite(area.right)||!std::isfinite(area.bottom)||area.right<area.left||area.bottom<area.top)throw std::invalid_argument("Invalid spatial query");
    // Small local queries touch only their cells. Broad overview queries scan
    // occupied cells instead, so empty space never creates unbounded work.
    const double left=std::floor(area.left/cell_size_),right=std::floor(area.right/cell_size_);
    const double top=std::floor(area.top/cell_size_),bottom=std::floor(area.bottom/cell_size_);
    if(left>=-1e8&&right<=1e8&&top>=-1e8&&bottom<=1e8&&
       (right-left+1)*(bottom-top+1)<=static_cast<double>(cells_.size())) {
      for(int y=static_cast<int>(top);y<=static_cast<int>(bottom);++y)
        for(int x=static_cast<int>(left);x<=static_cast<int>(right);++x)
          if(auto found=cells_.find({x,y});found!=cells_.end())result.insert(result.end(),found->second.begin(),found->second.end());
    } else {
      for(const auto& [cell_id,ids]:cells_){const double x=cell_id.first*cell_size_,y=cell_id.second*cell_size_;if(x>area.right||x+cell_size_<area.left||y>area.bottom||y+cell_size_<area.top)continue;result.insert(result.end(),ids.begin(),ids.end());}
    }
    std::ranges::sort(result);result.erase(std::unique(result.begin(),result.end()),result.end());
    std::erase_if(result,[&](std::size_t id){const auto& b=bounds_.at(id);return b.left>area.right||b.right<area.left||b.top>area.bottom||b.bottom<area.top;});return result;
  }
private:
  int cell(double v)const{return static_cast<int>(std::floor(v/cell_size_));}
  double cell_size_;
  std::map<std::pair<int,int>,std::vector<std::size_t>> cells_;
  std::map<std::size_t,SpatialBounds> bounds_;
};
} // namespace stellar::engine
