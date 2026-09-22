#pragma once

#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace stellar::native_map {


inline std::string hud_amount(double amount) {
  std::ostringstream s;
  if(std::abs(amount)>=1000000.)s<<std::fixed<<std::setprecision(1)<<amount/1000000.<<"M";
  else if(std::abs(amount)>=10000.)s<<std::fixed<<std::setprecision(1)<<amount/1000.<<"K";
  else s<<std::fixed<<std::setprecision(0)<<amount;
  return s.str();
}

inline void hud_text(DrawList& out,UiRect box,std::string value,int font,Color color,
                     TextAlign align=TextAlign::Left) {
  out.overlay.emplace_back(Text{{align==TextAlign::Center?box.x+box.width*.5f:box.x,box.y},
      std::move(value),color,font,box.width,box,align,FontFace::Heading});
}

inline void render_context_plate(DrawList& out,const CommandHudLayout& l,
    std::string title,std::string subtitle,bool paused,Point pointer,
    std::shared_ptr<const RgbaImage> crest,std::shared_ptr<const RgbaImage> destination,
    bool can_enter) {
  const auto p=l.context; const float s=l.scale;
  const Color accent=paused?Color{239,173,67,255}:Color{87,188,185,255};
  const Color base=paused?Color{33,25,16,245}:Color{7,25,33,246};
  TriangleMesh plate;plate.color=base;
  plate.vertices={{p.x+16*s,p.y},{p.x+p.width-16*s,p.y},{p.x+p.width,p.y+16*s},
      {p.x+p.width,p.y+p.height},{p.x,p.y+p.height},{p.x,p.y+16*s}};
  plate.indices={0,1,2,0,2,3,0,3,4,0,4,5};out.overlay.emplace_back(std::move(plate));
  out.overlay.emplace_back(Line{{p.x+16*s,p.y},{p.x+p.width-16*s,p.y},accent});
  out.overlay.emplace_back(Line{{p.x,p.y+p.height},{p.x+p.width,p.y+p.height},accent});
  if(crest)out.overlay.emplace_back(Image{crest,l.crest});
  const UiRect titlebox{p.x+56*s,p.y+(paused?20.f:10.f)*s,p.width-114*s,22*s};
  if(paused)hud_text(out,{titlebox.x,p.y+3*s,titlebox.width,16*s},"PAUSED",static_cast<int>(12*s),accent,TextAlign::Center);
  hud_text(out,titlebox,std::move(title),static_cast<int>(18*s),{236,244,247,255},TextAlign::Center);
  hud_text(out,{titlebox.x,p.y+(paused?41.f:34.f)*s,titlebox.width,16*s},std::move(subtitle),static_cast<int>(11*s),{142,204,198,255},TextAlign::Center);
  stellar::native_ui_style::panel(out,l.switch_view,l.switch_view.contains(pointer),false);
  if(destination)out.overlay.emplace_back(Image{destination,
      {l.switch_view.x+3*s,l.switch_view.y+3*s,l.switch_view.width-6*s,l.switch_view.height-6*s},
      std::nullopt,Color{255,255,255,255}});
  else {
    const Point center{l.switch_view.x+l.switch_view.width*.5f,l.switch_view.y+l.switch_view.height*.5f};
    const Color orbit{74,133,162,255};
    for(const float radius:{10.f,16.f})for(int i=0;i<48;++i){
      const float a=i*6.2831853f/48.f,b=(i+1)*6.2831853f/48.f;
      out.overlay.emplace_back(Line{{center.x+std::cos(a)*radius*s,center.y+std::sin(a)*radius*s*.62f},
          {center.x+std::cos(b)*radius*s,center.y+std::sin(b)*radius*s*.62f},orbit});
    }
    const auto disc=[&](Point c,float radius,Color color){
      TriangleMesh mesh;mesh.color=color;mesh.vertices.push_back(c);
      for(int i=0;i<=24;++i){
        const float a=i*6.2831853f/24.f;
        mesh.vertices.push_back({c.x+std::cos(a)*radius,c.y+std::sin(a)*radius});
        if(i>0)mesh.indices.insert(mesh.indices.end(),{0,i,i+1});
      }
      out.overlay.emplace_back(std::move(mesh));
    };
    disc(center,5*s,can_enter?Color{255,219,133,255}:Color{130,144,155,255});
    disc({center.x+14*s,center.y-5*s},2*s,{120,208,244,255});
  }
}

} // namespace stellar::native_map
