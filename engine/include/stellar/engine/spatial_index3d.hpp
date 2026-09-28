#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace stellar::engine {
// Balanced immutable geometry; replaceable opaque partitions permit efficient
// nearest-outside-component queries. Results use input indices for exact ties.
// Query scratch is local. Do not relabel concurrently with queries.
class SpatialIndex3D {
 public:
  using Point=std::array<double,3>;
  explicit SpatialIndex3D(std::vector<Point> points):points_(std::move(points)),order_(points_.size()){
    for(auto p:points_)for(auto x:p)if(!std::isfinite(x)||std::abs(x)>1e12)throw std::invalid_argument("Invalid spatial coordinate");
    std::iota(order_.begin(),order_.end(),std::size_t{});nodes_.reserve(points_.size()*2);
    if(!points_.empty())build(0,points_.size());
  }
  void partitions(std::span<const std::size_t> labels){
    if(labels.size()!=points_.size()||std::find(labels.begin(),labels.end(),mixed)!=labels.end())throw std::invalid_argument("Spatial partition size mismatch");
    labels_.assign(labels.begin(),labels.end());if(!nodes_.empty())label(0);
  }
  struct Match{std::size_t index;double distance;};
  // metric must be squared Euclidean distance, permitting legacy float
  // rounding. Conservative bounds include its rounding envelope.
  template<class Metric> std::vector<Match> nearest(std::size_t query,std::size_t count,
      bool outside_partition,const Metric& metric)const{
    if(query>=points_.size()||count>64||(outside_partition&&labels_.empty()))throw std::invalid_argument("Invalid spatial query");
    std::vector<Match> matches;matches.reserve(count);if(count)visit(0,query,count,outside_partition,metric,matches);return matches;
  }
 private:
  static constexpr std::size_t mixed=static_cast<std::size_t>(-1);
  struct Node{Point lo{},hi{};std::size_t begin{},end{},left{mixed},right{mixed},partition{mixed};};
  std::size_t build(std::size_t begin,std::size_t end){
    const auto id=nodes_.size();Node node;node.begin=begin;node.end=end;node.lo=node.hi=points_[order_[begin]];
    for(auto i=begin+1;i<end;++i)for(std::size_t d=0;d<3;++d){node.lo[d]=std::min(node.lo[d],points_[order_[i]][d]);node.hi[d]=std::max(node.hi[d],points_[order_[i]][d]);}
    nodes_.push_back(node);
    if(end-begin>16){std::size_t axis=0;for(std::size_t d=1;d<3;++d)if(node.hi[d]-node.lo[d]>node.hi[axis]-node.lo[axis])axis=d;
      const auto mid=begin+(end-begin)/2;
      std::nth_element(order_.begin()+begin,order_.begin()+mid,order_.begin()+end,[&](auto a,auto b){return points_[a][axis]!=points_[b][axis]?points_[a][axis]<points_[b][axis]:a<b;});
      const auto left=build(begin,mid),right=build(mid,end);nodes_[id].left=left;nodes_[id].right=right;
    }return id;
  }
  std::size_t label(std::size_t id){auto& n=nodes_[id];
    if(n.left!=mixed){const auto a=label(n.left),b=label(n.right);n.partition=a==b?a:mixed;}
    else{n.partition=labels_[order_[n.begin]];for(auto i=n.begin+1;i<n.end;++i)if(labels_[order_[i]]!=n.partition){n.partition=mixed;break;}}
    return n.partition;
  }
  double bound(const Node& n,const Point& p)const{double value=0;for(std::size_t d=0;d<3;++d){const auto delta=std::max({n.lo[d]-p[d],p[d]-n.hi[d],0.});value+=delta*delta;}return std::max(0.,value*(1.-1e-6)-1e-12);}
  template<class Metric> void visit(std::size_t id,std::size_t query,std::size_t count,bool outside,const Metric& metric,std::vector<Match>& out)const{
    const auto& n=nodes_[id];if(outside&&n.partition==labels_[query])return;
    if(out.size()==count&&bound(n,points_[query])>out.back().distance)return;
    if(n.left!=mixed){auto a=n.left,b=n.right;if(bound(nodes_[b],points_[query])<bound(nodes_[a],points_[query]))std::swap(a,b);visit(a,query,count,outside,metric,out);visit(b,query,count,outside,metric,out);return;}
    for(auto i=n.begin;i<n.end;++i){const auto candidate=order_[i];if(candidate==query||(outside&&labels_[candidate]==labels_[query]))continue;
      const auto distance=metric(query,candidate);if(!std::isfinite(distance)||distance<0)throw std::invalid_argument("Invalid spatial metric");
      const Match match{candidate,distance};const auto at=std::lower_bound(out.begin(),out.end(),match,[](const Match& a,const Match& b){return a.distance!=b.distance?a.distance<b.distance:a.index<b.index;});
      if(at!=out.end()||out.size()<count){out.insert(at,match);if(out.size()>count)out.pop_back();}
    }
  }
  std::vector<Point> points_;std::vector<std::size_t> order_,labels_;std::vector<Node> nodes_;
};
} // namespace stellar::engine
