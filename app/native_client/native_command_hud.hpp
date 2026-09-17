#pragma once

#include "native_colony_roster.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
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
      std::nullopt,can_enter?Color{255,255,255,255}:Color{115,132,142,255}});
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

inline void render_planet_outliner(DrawList& out,const CommandHudLayout& l,
    const stellar::native_colony_roster::View& view,float scroll,Point pointer,
    const std::function<std::shared_ptr<const RgbaImage>(int)>& portrait) {
  const float s=l.scale;const auto p=l.planets,clip=l.planet_list;
  stellar::native_ui_style::menu_panel(out,p);
  hud_text(out,{p.x+12*s,p.y+8*s,p.width-24*s,20*s},"PLANETS  /  "+std::to_string(view.rows.size()),static_cast<int>(12*s),{145,219,206,255});
  if(view.rows.empty())hud_text(out,clip,"No owned worlds",static_cast<int>(12*s),{152,178,193,255});
  for(std::size_t i=0;i<view.rows.size();++i){
    const auto r=l.row(i,scroll);if(r.y+r.height<=clip.y||r.y>=clip.y+clip.height)continue;
    const auto& row=view.rows[i];const bool hover=clip.contains(pointer)&&r.contains(pointer);
    const UiRect visible{r.x,std::max(r.y,clip.y),r.width,std::min(r.y+r.height,clip.y+clip.height)-std::max(r.y,clip.y)};
    out.overlay.emplace_back(FilledRectangle{visible,hover?Color{23,59,66,245}:Color{9,25,34,242}});
    if(auto image=portrait(row.body_id))out.overlay.emplace_back(Image{image,{r.x+4*s,r.y+6*s,34*s,34*s},std::nullopt,{255,255,255,255},clip});
    const auto label=[&](float y,std::string text,int size,Color color){
      out.overlay.emplace_back(Text{{r.x+44*s,y},std::move(text),color,size,r.width-50*s,clip});
    };
    label(r.y+5*s,row.name,static_cast<int>(14*s),row.can_open?Color{220,240,240,255}:Color{155,169,179,255});
    label(r.y+25*s,row.system_name+"  ·  "+row.population,static_cast<int>(11*s),{136,174,186,255});
  }
  const float content=view.rows.size()*l.row_height;
  if(content>clip.height){
    const float thumb=std::max(16*s,clip.height*clip.height/content);
    out.overlay.emplace_back(FilledRectangle{{p.x+p.width-4*s,clip.y+(clip.height-thumb)*scroll/(content-clip.height),2*s,thumb},{99,199,184,255}});
  }
}
} // namespace stellar::native_map
