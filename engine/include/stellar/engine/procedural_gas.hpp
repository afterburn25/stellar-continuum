#pragma once
#include <stellar/engine/organic_region.hpp>

namespace stellar::engine {
// Presentation-only gas structure: warped turbulent bands, fine illuminated
// filaments and absorbing dust. The caller supplies the authoritative bounds.
struct GasSample { double density{}, filament{}, dust{}; };
inline GasSample sample_gas(std::uint64_t seed,double x,double y) {
  const double wx=region_noise(seed^481,x*2.7,y*2.7)-.5;
  const double wy=region_noise(seed^919,x*2.7+19,y*2.7-7)-.5;
  const double u=x+wx*.38,v=y+wy*.38;
  const double broad=region_noise(seed,u*5,v*5)*.58+
      region_noise(seed^73,u*11,v*11)*.28+region_noise(seed^139,u*23,v*23)*.14;
  const double turbulent=region_noise(seed^907,u*29,v*29)*.48+
      region_noise(seed^1721,u*61,v*61)*.34+region_noise(seed^373,u*127,v*127)*.18;
  // Anisotropic density folds form wisps, without contour-line ridges that
  // resemble water caustics. Fine structure modulates the whole gas volume.
  const double folds=region_noise(seed^309,u*57,v*15)*.64+
      region_noise(seed^601,u*109,v*23)*.36;
  const double strand=std::clamp((folds-.38)*2.5,0.,1.);
  const double filament=strand*strand*(.3+.7*turbulent);
  const double grain=.4+.6*region_noise(seed^2311,u*233,v*233);
  const double dust=std::clamp((region_noise(seed^701,u*8,v*8)*.7+
      region_noise(seed^977,u*31,v*31)*.3-.35)*2.4,0.,1.);
  const double body=std::clamp((broad-.29)*2.4,0.,1.);
  return {std::clamp(std::pow(body,1.2)*(.2+.65*turbulent+1.05*filament)*grain*(1.-.85*dust*dust),0.,1.),filament,dust};
}
} // namespace stellar::engine
