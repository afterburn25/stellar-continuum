#include <stellar/engine/ring_material_preparation.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace stellar::native_map {
namespace {
using RGB=std::array<double,3>;constexpr double pi=std::numbers::pi;
double level(RGB p){return std::max({p[0],p[1],p[2]});}
double quantile(std::vector<double> v,double q){if(v.empty())return 0;const auto i=static_cast<std::size_t>(q*(v.size()-1));std::nth_element(v.begin(),v.begin()+i,v.end());return v[i];}
RGB sample(const RgbaImage& image,double x,double y,RGB background){if(x<0||y<0||x>=image.width()-1||y>=image.height()-1)return {};const int ix=static_cast<int>(x),iy=static_cast<int>(y);RGB p{};for(int j=0;j<2;++j)for(int i=0;i<2;++i){const double w=(i?x-ix:1-x+ix)*(j?y-iy:1-y+iy);const auto at=(static_cast<std::size_t>(iy+j)*image.width()+ix+i)*4;for(int k=0;k<3;++k)p[k]+=std::max(0.,image.pixels()[at+k]/255.-background[k])*w;}return p;}
std::uint8_t byte(double x){return static_cast<std::uint8_t>(std::clamp(std::lround(x*255),0L,255L));}
}
RingMaterialImages prepare_ring_material(const RgbaImage& image,const RingSourceOptions& o){
 if(o.radial_samples<128||o.radial_samples>4096||o.azimuth_samples<16||o.azimuth_samples>4096)throw std::invalid_argument("Invalid ring preparation resolution");
 RingMaterialImages result;RGB background{};std::array<std::vector<double>,3> corners;
 for(int y=0;y<image.height();y+=8)for(int x=0;x<image.width();x+=8)if((x<image.width()/12||x>image.width()*11/12)&&(y<image.height()/12||y>image.height()*11/12)){auto at=(static_cast<std::size_t>(y)*image.width()+x)*4;for(int c=0;c<3;++c)corners[c].push_back(image.pixels()[at+c]/255.);}
 for(int c=0;c<3;++c)background[c]=quantile(corners[c],.6);
 double peak=0;for(int y=0;y<image.height();y+=4)for(int x=0;x<image.width();x+=4)peak=std::max(peak,level(sample(image,x,y,background)));
 const double threshold=std::max(.007,peak*.06);double sx=0,sy=0,sxx=0,syy=0,sxy=0,total=0;
 for(int y=0;y<image.height();y+=3)for(int x=0;x<image.width();x+=3){auto p=sample(image,x,y,background);if(level(p)<threshold)continue;const double w=std::pow(level(p),.08);sx+=x*w;sy+=y*w;sxx+=x*x*w;syy+=y*y*w;sxy+=x*y*w;total+=w;}
 if(total<100){result.rejection_reason="Insufficient resolved ring signal";return result;}
 double cx=sx/total,cy=sy/total,xx=sxx/total-cx*cx,yy=syy/total-cy*cy,xy=sxy/total-cx*cy;
 double angle=.5*std::atan2(2*xy,xx-yy),trace=xx+yy,delta=std::hypot(xx-yy,2*xy),a=std::sqrt(trace+delta),b=std::sqrt(std::max(1.,trace-delta));
 // Refine the ellipse from its outer edge. Robust least squares discards
 // luminous dust outliers and missing source-shadow sectors.
 for(int pass=0;pass<4;++pass){std::vector<std::array<double,3>> points;
  for(int sector=0;sector<180;++sector){double t=sector*2*pi/180,dx=a*std::cos(t)*std::cos(angle)-b*std::sin(t)*std::sin(angle),dy=a*std::cos(t)*std::sin(angle)+b*std::sin(t)*std::cos(angle),best=0,radius=0;
   for(int n=0;n<180;++n){const double r=.65+n*.0045;const double inside=level(sample(image,cx+dx*(r-.005),cy+dy*(r-.005),background)),outside=level(sample(image,cx+dx*(r+.005),cy+dy*(r+.005),background));const double edge=std::max(0.,inside-outside)*std::pow(r,3);if(edge>best&&inside>threshold){best=edge;radius=r;}}
   if(best>.008)points.push_back({(cx+dx*radius)/image.width(),(cy+dy*radius)/image.height(),best});
  }
  if(points.size()<45)break;
  double matrix[5][6]{};for(const auto& p:points){double x=p[0]-.5,y=p[1]-.5,f[]{x*x,x*y,y*y,x,y};const double w=std::min(.2,p[2]);for(int i=0;i<5;++i){for(int j=0;j<5;++j)matrix[i][j]+=w*f[i]*f[j];matrix[i][5]+=w*f[i];}}
  bool singular=false;for(int i=0;i<5;++i){int pivot=i;for(int j=i+1;j<5;++j)if(std::abs(matrix[j][i])>std::abs(matrix[pivot][i]))pivot=j;for(int k=0;k<6;++k)std::swap(matrix[i][k],matrix[pivot][k]);if(std::abs(matrix[i][i])<1e-12){singular=true;break;}double d=matrix[i][i];for(int k=i;k<6;++k)matrix[i][k]/=d;for(int j=0;j<5;++j)if(j!=i){double f=matrix[j][i];for(int k=i;k<6;++k)matrix[j][k]-=f*matrix[i][k];}}
  if(singular)break;const double A=matrix[0][5],B=matrix[1][5]/2,C=matrix[2][5],D=matrix[3][5]/2,E=matrix[4][5]/2,det=A*C-B*B;if(det<=0||A<=0||C<=0)break;
  const double ex=(B*E-C*D)/det,ey=(B*D-A*E)/det,F=1-D*ex-E*ey;
  const double aa=A/(image.width()*image.width()),bb=B/(image.width()*image.height()),cc=C/(image.height()*image.height()),dd=std::hypot(aa-cc,2*bb);
  const double na=std::sqrt(F/((aa+cc-dd)*.5)),nb=std::sqrt(F/((aa+cc+dd)*.5)),ncx=(ex+.5)*image.width(),ncy=(ey+.5)*image.height();
  if(!std::isfinite(na)||!std::isfinite(nb)||na>image.width()||nb<10||std::hypot(ncx-cx,ncy-cy)>image.width()*.15)break;
  cx=ncx;cy=ncy;a=na;b=nb;angle=.5*std::atan2(2*bb,aa-cc)+pi*.5;
 }
 result.ellipse={cx,cy,a,b,angle};if(a<60||b<12){result.rejection_reason="Projected ring too thin or unresolved";return result;}
 constexpr int radial=2048,angles=128;std::vector<RGB> samples(radial*angles);std::vector<double> profile(radial),means(angles);
 for(int j=0;j<angles;++j){double t=j*2*pi/angles,dx=a*std::cos(t)*std::cos(angle)-b*std::sin(t)*std::sin(angle),dy=a*std::cos(t)*std::sin(angle)+b*std::sin(t)*std::cos(angle);for(int i=0;i<radial;++i){double r=.15+1.02*i/(radial-1);auto p=sample(image,cx+dx*r,cy+dy*r,background);samples[j*radial+i]=p;means[j]+=level(p)/radial;}}
 const double median_mean=quantile(means,.65);
 for(int i=0;i<radial;++i){std::vector<double> values;for(int j=0;j<angles;++j){auto& p=samples[j*radial+i];const double correction=std::clamp(median_mean/std::max(.001,means[j]),.5,2.);for(auto& c:p)c*=correction;values.push_back(level(p));}profile[i]=quantile(values,.65);}
 const double profile_peak=*std::max_element(profile.begin(),profile.end());if(profile_peak<.012){result.rejection_reason="No coherent radial material recovered";return result;}
 int first=0,last=radial-1;while(first<last&&profile[first]<profile_peak*.025)++first;while(last>first&&profile[last]<profile_peak*.025)--last;first=std::max(0,first-3);last=std::min(radial-1,last+3);
 result.inner_fraction=.15+1.02*first/(radial-1);result.outer_fraction=.15+1.02*last/(radial-1);
 if(last-first<4){result.rejection_reason="No resolved ring width";return result;}
 const int w=o.radial_samples,h=o.azimuth_samples;std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4);
 // A ring image is not a perfectly concentric mathematical ellipse. Averaging
 // different rays at one radius smears its narrow gaps even at infinite output
 // resolution. Sample the original pixels directly, retaining those variations.
 for(int y=0;y<h;++y){
  const double phase=static_cast<double>(y)/(h-1),t=phase*2*pi;
  const double sector=phase*angles;const int j=static_cast<int>(sector)%angles;
  const double f=sector-std::floor(sector),mean=means[j]*(1-f)+means[(j+1)%angles]*f;
  const double correction=std::clamp(median_mean/std::max(.001,mean),.5,2.);
  const double dx=a*std::cos(t)*std::cos(angle)-b*std::sin(t)*std::sin(angle),dy=a*std::cos(t)*std::sin(angle)+b*std::sin(t)*std::cos(angle);
  for(int x=0;x<w;++x){
   const double r=result.inner_fraction+(result.outer_fraction-result.inner_fraction)*x/(w-1);
   auto c=sample(image,cx+dx*r,cy+dy*r,background);for(auto& v:c)v*=correction;
   const double high=level(c),opacity=std::clamp(high/profile_peak,0.,1.);
   const auto at=(static_cast<std::size_t>(y)*w+x)*4;
   // Straight alpha avoids black fringes. Only broad illumination is removed;
   // original chromatic detail and fine optical-density bands remain intact.
   for(int k=0;k<3;++k)pixels[at+k]=byte(high>.0001?c[k]/high*.8:.8);
   pixels[at+3]=byte(opacity);result.mean_opacity+=opacity/(w*h);
   if(opacity<.07)result.gap_fraction+=1./(w*h);
  }
 }
 result.material=RgbaImage::create(w,h,std::move(pixels));
 constexpr int size=384;std::vector<std::uint8_t> preview(size*size*4,0);for(int y=0;y<size;++y)for(int x=0;x<size;++x){auto at=(y*size+x)*4;const double nx=(x+.5-size/2.)/(size*.46),ny=(y+.5-size/2.)/(size*.46),r=std::hypot(nx,ny);double opacity=0;RGB rgb{};if(r>=result.inner_fraction/result.outer_fraction&&r<=1){double u=(r*result.outer_fraction-result.inner_fraction)/(result.outer_fraction-result.inner_fraction),v=std::atan2(ny,nx)/(2*pi);v-=std::floor(v);auto src=(static_cast<std::size_t>(std::min(h-1,static_cast<int>(v*h)))*w+std::min(w-1,static_cast<int>(u*w)))*4;opacity=result.material->pixels()[src+3]/255.;for(int k=0;k<3;++k)rgb[k]=result.material->pixels()[src+k]/255.;}for(int k=0;k<3;++k)preview[at+k]=byte(rgb[k]*opacity+.025*(1-opacity));preview[at+3]=255;}
 result.preview=RgbaImage::create(size,size,std::move(preview));result.usable=true;return result;
}
}
