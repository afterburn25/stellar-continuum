#pragma once
#include "native_dropdown.hpp"
#include "../campaign_diagnostic_monitor.hpp"
#include <stellar/core/campaign_world_projection.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/profiler.hpp>
#include <iomanip>
#include <sstream>

namespace stellar::native_map {
namespace {
const char *campaign_domain_name(std::int64_t domain){
  using D=stellar::core::CampaignDomain;
  switch(static_cast<D>(domain)){
    case D::System:return "system";case D::Body:return "body";
    case D::Civilization:return "civilization";case D::Colony:return "colony";
    case D::Fleet:return "fleet";case D::Economy:return "economy";
    case D::Technology:return "technology";case D::Construction:return "construction";
    case D::Shipyard:return "shipyard";
  }
  return "domain";
}
} // namespace
class NativeDeveloperDiagnostics {
public:
  void open(const stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor){visible_=true;events_=false;generation_=false;assets_=false;entities_=false;first_=0;refresh(monitor);}
  void close(){visible_=false;pressed_=-1;dropdown_.close();}
  bool visible()const{return visible_;}
  bool handle(const InputEvent &e,int w,int h,stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor){
    if(!visible_)return false;const auto l=layout(w,h);pointer_=e.position;
    if(dropdown_.visible()){
      if(const auto choice=dropdown_.handle(e,l.detail,w,h))monitor.history().set_detail(static_cast<stellar::engine::DiagnosticDetail>(*choice));
      return true;
    }
    if(e.type==InputEventType::EscapePressed){close();return true;}
    if(e.type==InputEventType::PointerCancelled){pressed_=-1;return true;}
    if(e.type==InputEventType::Wheel&&l.list.contains(e.position))first_=std::max(0,first_-static_cast<int>(std::round(e.wheel_y)));
    if(e.type==InputEventType::LeftPressed){
      if(l.detail.contains(e.position)){dropdown_.open(0,{"Errors only","Normal","Detailed","Trace"},static_cast<int>(monitor.history().detail()));return true;}
      pressed_=l.close.contains(e.position)?0:l.performance.contains(e.position)?1:l.events.contains(e.position)?2:l.refresh.contains(e.position)?3:l.generation.contains(e.position)?4:l.assets.contains(e.position)?5:l.entities.contains(e.position)?6:-1;
    }
    if(e.type==InputEventType::LeftReleased){
      const int hit=std::exchange(pressed_,-1);
      if(hit==0&&l.close.contains(e.position))close();
      if(hit==1&&l.performance.contains(e.position)){events_=false;generation_=false;assets_=false;entities_=false;first_=0;}
      if(hit==2&&l.events.contains(e.position)){events_=true;generation_=false;assets_=false;entities_=false;first_=0;refresh(monitor);}
      if(hit==3&&l.refresh.contains(e.position)){first_=0;refresh(monitor);entities_dirty_=true;}
      if(hit==4&&l.generation.contains(e.position)){generation_=true;assets_=false;entities_=false;first_=0;}
      if(hit==5&&l.assets.contains(e.position)){assets_=true;generation_=false;entities_=false;first_=0;}
      if(hit==6&&l.entities.contains(e.position)){entities_=true;events_=false;generation_=false;assets_=false;first_=0;entities_dirty_=true;}
    }
    return true;
  }
  void render(DrawList &out,int w,int h,stellar::core::CampaignFrame &frame,
      const stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor)const{
    if(!visible_)return;const auto l=layout(w,h);const auto s=l.scale;const int font=std::max(12,static_cast<int>(16*s));
    native_menu_style::panel(out,l.panel,s);
    const auto label=[&](UiRect r,std::string value,Color color=native_menu_style::ink){native_menu_style::text(out,r,std::move(value),font,color);};
    const auto button=[&](UiRect r,std::string name){native_menu_style::button(out,r,std::move(name),font,r.contains(pointer_),true,s);};
    label({l.panel.x+20*s,l.panel.y+16*s,850*s,28*s},"DEVELOPER · PERFORMANCE & DIAGNOSTICS",native_menu_style::cyan);
    button(l.close,"CLOSE");button(l.performance,"LIVE PERFORMANCE");button(l.events,"RECENT EVENTS");button(l.refresh,"REFRESH EVENTS");button(l.generation,"GALAXY DETAILS");button(l.assets,"COOKED ASSETS");button(l.entities,"ENTITIES");
    const std::array<std::string,4> levels{"Errors only","Normal","Detailed","Trace"};
    button(l.detail,"Record: "+levels.at(static_cast<std::size_t>(monitor.history().detail()))+" ▾");
    if(assets_){
      if(auto registry=stellar::engine::mounted_asset_registry()){
        const auto d=registry->diagnostics();const auto& records=registry->records();
        label({l.list.x,l.list.y-31*s,l.list.width,27*s},std::to_string(d.assets)+" assets / "+std::to_string(d.packages)+" packages · Reads "+std::to_string(d.reads)+" · Errors "+std::to_string(d.failures)+" · Read "+number(d.bytes_read/1048576.)+" MiB",native_menu_style::muted);
        const int begin=std::clamp(first_,0,std::max(0,static_cast<int>(records.size())-7));
        for(int i=0;i<7&&begin+i<static_cast<int>(records.size());++i){const auto&r=records[begin+i];std::uint64_t size=0;for(const auto&c:r.chunks)size+=c.stored_bytes;const auto y=l.list.y+i*61*s;
          label({l.list.x,y,l.list.width,25*s},r.id);
          label({l.list.x,y+27*s,l.list.width,25*s},r.format+" · "+std::to_string(r.width)+" × "+std::to_string(r.height)+" · "+std::to_string(r.chunks.size())+" chunks · "+number(size/1048576.)+" MiB · "+r.chunks.front().package,native_menu_style::muted);
        }
      }else label(l.list,"Development source mode. Cook and launch a packaged build to inspect runtime compression and package reads.");
    }else if(generation_){
      const auto& world=frame.runtime().world().campaign();
      if(world.generation_metadata&&world.generation_metadata->configuration){
        const auto& c=*world.generation_metadata->configuration;const auto pair=stellar::core::galaxy_visual_pair(c.morphology,c.resolved_population);
        label(l.list,stellar::core::galaxy_configuration_description(c)+"\n\nPreview: "+c.preview_asset_id+"\nMap: "+c.map_asset_id+"\n"+pair.diagnostic);
      }else label(l.list,"This legacy save predates canonical generation fingerprints. Its saved galaxy remains authoritative.");
    }else if(entities_){
      // Read-only projection of the authoritative campaign into the typed
      // World store — rebuilt on open/refresh, never on every frame.
      if(entities_dirty_){
        entity_lines_.clear();entity_parented_=0;
        const auto projected=stellar::core::project_campaign_world(frame.runtime().world().campaign());
        entity_bytes_=projected.estimated_memory_bytes();
        entity_lines_.reserve(projected.size());
        const auto legacy_name=[&](stellar::engine::EntityId e){
          if(const auto legacy=projected.legacy_for(e))
            return std::string(campaign_domain_name(*legacy>>32))+" "+std::to_string(static_cast<int>(static_cast<std::uint32_t>(*legacy)));
          return std::string("entity");
        };
        // Tag fields are the component payload — the drill-down surface
        // shows the authoritative refs each tag carries.
        const auto tag_detail=[&](stellar::engine::EntityId e){
          using namespace stellar::core;
          std::string detail;
          if(const auto*tag=projected.get<CampaignSystemTag>(e))detail="x "+number(tag->x)+", y "+number(tag->y);
          else if(const auto*body=projected.get<CampaignBodyTag>(e))detail="system "+std::to_string(body->system_id);
          else if(const auto*civ=projected.get<CampaignCivilizationTag>(e))detail="home system "+std::to_string(civ->home_system_id);
          else if(const auto*colony=projected.get<CampaignColonyTag>(e))detail="civ "+std::to_string(colony->civilization_id)+" · system "+std::to_string(colony->system_id);
          else if(const auto*fleet=projected.get<CampaignFleetTag>(e))detail="civ "+std::to_string(fleet->civilization_id);
          else if(const auto*economy=projected.get<CampaignEconomyTag>(e))detail="civ "+std::to_string(economy->civilization_id);
          else if(const auto*tech=projected.get<CampaignTechnologyTag>(e))detail="civ "+std::to_string(tech->civilization_id);
          else if(const auto*construction=projected.get<CampaignConstructionTag>(e))detail="civ "+std::to_string(construction->civilization_id);
          else if(const auto*shipyard=projected.get<CampaignShipyardTag>(e))detail="civ "+std::to_string(shipyard->civilization_id);
          return detail;
        };
        for(const auto e:projected.entities()){
          std::string line=legacy_name(e);
          if(const auto detail=tag_detail(e);!detail.empty())line+="   ·   "+detail;
          if(const auto p=projected.parent(e)){++entity_parented_;line+="   ←   "+legacy_name(*p);}
          entity_lines_.push_back(std::move(line));
        }
        entities_dirty_=false;
      }
      label({l.list.x,l.list.y-31*s,l.list.width,27*s},std::to_string(entity_lines_.size())+" projected entities · "+std::to_string(entity_parented_)+" parented · "+number(entity_bytes_/1024.)+" KiB estimated container footprint · read-only projection",native_menu_style::muted);
      const auto begin=std::clamp(first_,0,std::max(0,static_cast<int>(entity_lines_.size())-14));
      for(int i=0;i<14&&begin+i<static_cast<int>(entity_lines_.size());++i){
        const auto y=l.list.y+i*33*s;
        if(i%2==0)out.overlay.emplace_back(FilledRectangle{{l.list.x,y,l.list.width,31*s},{12,32,45,210}});
        label({l.list.x+8*s,y+4*s,l.list.width-16*s,25*s},entity_lines_[begin+i]);
      }
      if(entity_lines_.empty())label(l.list,"The campaign projected no entities.");
    }else if(!events_){
      label({l.list.x+8*s,l.list.y-31*s,400*s,27*s},"Phase",native_menu_style::muted);
      label({l.list.x+430*s,l.list.y-31*s,140*s,27*s},"Samples",native_menu_style::muted);
      label({l.list.x+600*s,l.list.y-31*s,170*s,27*s},"Mean ms",native_menu_style::muted);
      label({l.list.x+810*s,l.list.y-31*s,190*s,27*s},"Maximum ms",native_menu_style::muted);
      const auto samples=frame.runtime().performance_samples();
      struct Row{std::string phase;std::uint64_t n,total,maximum;};
      std::vector<Row> rows;rows.reserve(samples.size()+8);
      for(const auto&p:samples)rows.push_back({std::string(p.phase),p.timing.samples,p.timing.total_nanoseconds,p.timing.maximum_nanoseconds});
      for(const auto&a:stellar::engine::Profiler::instance().aggregates())
        rows.push_back({"client/"+a.name,a.calls,a.total_nanoseconds,a.max_nanoseconds});
      const auto begin=std::clamp(first_,0,std::max(0,static_cast<int>(rows.size())-14));
      for(int i=0;i<14&&begin+i<static_cast<int>(rows.size());++i){
        const auto &p=rows[begin+i];const auto y=l.list.y+i*33*s;
        if(i%2==0)out.overlay.emplace_back(FilledRectangle{{l.list.x,y,l.list.width,31*s},{12,32,45,210}});
        label({l.list.x+8*s,y+4*s,400*s,25*s},p.phase);
        label({l.list.x+430*s,y+4*s,140*s,25*s},std::to_string(p.n));
        label({l.list.x+600*s,y+4*s,170*s,25*s},p.n?number(static_cast<double>(p.total)/p.n/1e6):"Unmeasured");
        label({l.list.x+810*s,y+4*s,190*s,25*s},p.n?number(p.maximum/1e6):"Unmeasured");
      }
    }else{
      label({l.list.x,l.list.y-31*s,l.list.width,27*s},std::to_string(snapshot_.size())+" retained events · newest first · snapshot captured on refresh",native_menu_style::muted);
      const auto begin=std::clamp(first_,0,std::max(0,static_cast<int>(snapshot_.size())-8));
      for(int i=0;i<8&&begin+i<static_cast<int>(snapshot_.size());++i){
        const auto &r=snapshot_[begin+i];const auto y=l.list.y+i*57*s;
        out.overlay.emplace_back(FilledRectangle{{l.list.x,y,l.list.width,54*s},{12,32,45,210}});
        const auto color=r.severity>=stellar::engine::DiagnosticSeverity::Error?Color{255,135,112,255}:
            r.severity==stellar::engine::DiagnosticSeverity::Warning?Color{245,199,113,255}:native_menu_style::cyan;
        label({l.list.x+8*s,y+3*s,l.list.width-16*s,23*s},r.game_date+" · tick "+std::to_string(r.tick)+(r.civilization_id?" · empire "+std::to_string(*r.civilization_id):"")+" · "+r.subsystem+" / "+r.event_type,color);
        label({l.list.x+8*s,y+28*s,l.list.width-16*s,23*s},r.message);
      }
      if(snapshot_.empty())label(l.list,"No retained events at the chosen recording level. This does not certify a clean simulation.",native_menu_style::muted);
    }
    label({l.panel.x+20*s,l.panel.y+650*s,l.panel.width-40*s,26*s},
      "Checks: "+std::to_string(monitor.invariant_checks())+" · Retained: "+std::to_string(monitor.history().records().size())+
      " · Overwritten: "+std::to_string(monitor.history().overwritten_records())+" · Filtered: "+std::to_string(monitor.history().filtered_records()),native_menu_style::muted);
    label({l.panel.x+20*s,l.panel.y+681*s,l.panel.width-40*s,24*s},
      "CPU timings are observations, not FPS. core_total includes its child phases.",native_menu_style::muted);
    label({l.panel.x+20*s,l.panel.y+710*s,l.panel.width-40*s,24*s},"Scroll for more. Export includes full retained event text.",native_menu_style::muted);
    if(dropdown_.visible())dropdown_.render(out,l.detail,w,h,font);
  }
private:
  struct Layout {float scale;UiRect panel,close,performance,events,refresh,detail,list,generation,assets,entities;};
  static Layout layout(int w,int h){
    const float s=std::clamp(std::min(w/1920.f,h/1080.f),.67f,2.f);const UiRect p{w*.5f-550*s,h*.5f-370*s,1100*s,740*s};
    return {s,p,{p.x+984*s,p.y+12*s,96*s,32*s},{p.x+20*s,p.y+64*s,242*s,38*s},
      {p.x+276*s,p.y+64*s,242*s,38*s},{p.x+532*s,p.y+64*s,242*s,38*s},{p.x+788*s,p.y+64*s,292*s,38*s},
      {p.x+20*s,p.y+186*s,1060*s,445*s},{p.x+20*s,p.y+110*s,242*s,34*s},{p.x+276*s,p.y+110*s,242*s,34*s},
      {p.x+532*s,p.y+110*s,242*s,34*s}};
  }
  bool generation_{},assets_{},entities_{};
  mutable bool entities_dirty_{true};
  mutable std::vector<std::string> entity_lines_;
  mutable int entity_parented_{};
  mutable std::size_t entity_bytes_{};
  void refresh(const stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor){
    snapshot_.clear();for(auto i=monitor.history().records().rbegin();i!=monitor.history().records().rend();++i)snapshot_.push_back(i->record);
  }
  static std::string number(double value){std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<value;return out.str();}
  std::vector<stellar::engine::DiagnosticRecord> snapshot_;stellar::native_ui::Dropdown dropdown_;
  bool visible_{},events_{};int pressed_{-1},first_{};Point pointer_{};
};
}
