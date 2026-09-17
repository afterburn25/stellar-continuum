#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace stellar::native_menu_style {
using namespace stellar::native_map;
inline constexpr Color ink{237,246,255,255}, muted{183,205,224,255}, cyan{127,221,240,255}, gold{239,206,137,255};
inline void rounded(DrawList& out, UiRect r, Color color, float radius=5.f, bool outline=false) {
  radius=std::max(0.f,std::min({radius,r.width*.5f,r.height*.5f}));
  std::vector<Point> points;
  constexpr float pi=3.14159265358979323846f;
  const Point centers[]={{r.x+r.width-radius,r.y+radius},{r.x+r.width-radius,r.y+r.height-radius},
                         {r.x+radius,r.y+r.height-radius},{r.x+radius,r.y+radius}};
  for(int corner=0;corner<4;++corner)for(int step=0;step<=5;++step){
    const float angle=(-.5f+corner*.5f+step*.1f)*pi;
    points.push_back({centers[corner].x+std::cos(angle)*radius,centers[corner].y+std::sin(angle)*radius});
  }
  if(outline){for(std::size_t i=0;i<points.size();++i)out.overlay.emplace_back(Line{points[i],points[(i+1)%points.size()],color});return;}
  TriangleMesh mesh;mesh.vertices=std::move(points);mesh.color=color;
  for(int i=1;i+1<static_cast<int>(mesh.vertices.size());++i){mesh.indices.push_back(0);mesh.indices.push_back(i);mesh.indices.push_back(i+1);}
  out.overlay.emplace_back(std::move(mesh));
}
inline void panel(DrawList& out,UiRect r,float scale=1.f){
  rounded(out,{r.x+5*scale,r.y+7*scale,r.width,r.height},{0,4,12,115},7*scale);
  rounded(out,r,{17,42,64,232},6*scale);
  rounded(out,r,{93,159,189,235},6*scale,true);
  out.overlay.emplace_back(Line{{r.x+8*scale,r.y+scale},{r.x+r.width-8*scale,r.y+scale},{160,220,240,65}});
}
inline void text(DrawList& out,UiRect r,std::string value,int size,Color color=ink,TextAlign align=TextAlign::Left){
  const float x=align==TextAlign::Center?r.x+r.width*.5f:align==TextAlign::Right?r.x+r.width:r.x;
  out.overlay.emplace_back(Text{{x,r.y},std::move(value),color,size,r.width,r,align,FontFace::Interface});
}
inline void button(DrawList& out,UiRect r,std::string title,int font,bool hover=false,bool enabled=true,float scale=1.f){
  rounded(out,r,enabled?(hover?Color{33,92,132,245}:Color{28,68,98,215}):Color{30,42,52,190},4*scale);
  rounded(out,r,enabled?(hover?cyan:Color{85,148,177,240}):Color{84,109,129,175},4*scale,true);
  text(out,{r.x+12*scale,r.y+(r.height-font)*.5f,r.width-24*scale,r.height},std::move(title),font,enabled?ink:muted);
}
}
