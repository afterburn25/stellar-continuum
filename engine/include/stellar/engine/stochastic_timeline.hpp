#pragma once
#include <stellar/engine/foundation.hpp>
#include <array>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace stellar::engine {
// Counter-based independent draws: scheduling is unaffected by frame size,
// traversal order, camera, graphics settings or unrelated random consumers.
inline double timeline_random(std::uint64_t seed,std::uint64_t counter,std::uint64_t channel=0){
  DeterministicRandom random(seed ^ (counter*0x9e3779b97f4a7c15ULL) ^ (channel*0xd1b54a32d192ed03ULL));
  return std::clamp(random.unit_double(),0x1p-53,1.-0x1p-53);
}
inline double exponential_interval(double rate,std::uint64_t seed,std::uint64_t counter){
  if(!std::isfinite(rate)||rate<=0)throw std::invalid_argument("Hazard rate must be finite and positive");
  return -std::log1p(-timeline_random(seed,counter))/rate;
}
struct TimelineSample { int stage{};double stage_fraction{},fraction{},elapsed{},remaining{};bool finished{}; };
inline TimelineSample sample_timeline(const std::array<double,4>& duration,double elapsed){
  double total=0;for(double d:duration){if(!std::isfinite(d)||d<=0)throw std::invalid_argument("Invalid timeline duration");total+=d;}
  if(!std::isfinite(elapsed))throw std::invalid_argument("Invalid timeline elapsed time");
  TimelineSample result;result.elapsed=std::clamp(elapsed,0.,total);result.remaining=total-result.elapsed;
  result.fraction=result.elapsed/total;result.finished=elapsed>=total;
  double local=result.elapsed;for(int i=0;i<4;++i){result.stage=i;result.stage_fraction=std::clamp(local/duration[i],0.,1.);if(local<duration[i])break;local-=duration[i];}
  return result;
}
inline double smooth_timeline_curve(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
} // namespace stellar::engine
