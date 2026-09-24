#pragma once
#include "native_dropdown.hpp"
#include "../campaign_diagnostic_monitor.hpp"
#include <stellar/core/campaign_world_projection.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <charconv>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_set>

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
  void open(const stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor){visible_=true;events_=false;generation_=false;assets_=false;entities_=false;list_view_.scroll_offset=0;refresh(monitor);}
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
    // Shared scroll model — row stride/count are view-dependent, so the
    // engine VirtualizedList is configured per event/render against the
    // last rendered row set (list_rows_ refreshes every frame).
    list_view_.row_height=(assets_?61.f:events_?57.f:33.f)*l.scale;
    list_view_.viewport_height=l.list.height;
    list_view_.row_count=list_rows_;
    if(e.type==InputEventType::Wheel&&l.list.contains(e.position))list_view_.scroll_to(list_view_.scroll_offset-std::round(e.wheel_y)*list_view_.row_height);
    if(e.type==InputEventType::LeftPressed){
      if(l.detail.contains(e.position)){dropdown_.open(0,{"Errors only","Normal","Detailed","Trace"},static_cast<int>(monitor.history().detail()));return true;}
      pressed_=l.close.contains(e.position)?0:l.performance.contains(e.position)?1:l.events.contains(e.position)?2:l.refresh.contains(e.position)?3:l.generation.contains(e.position)?4:l.assets.contains(e.position)?5:l.entities.contains(e.position)?6:-1;
      if(pressed_<0&&entities_&&!entities_dirty_&&entity_rows_rect(l).contains(e.position)){
        const int row=static_cast<int>((e.position.y-l.list.y+list_view_.scroll_offset)/list_view_.row_height);
        if(row>=0&&row<static_cast<int>(entity_flat_.size()))pressed_=100+row;
      }
    }
    if(e.type==InputEventType::LeftReleased){
      const int hit=std::exchange(pressed_,-1);
      if(hit==0&&l.close.contains(e.position))close();
      if(hit==1&&l.performance.contains(e.position)){events_=false;generation_=false;assets_=false;entities_=false;list_view_.scroll_offset=0;}
      if(hit==2&&l.events.contains(e.position)){events_=true;generation_=false;assets_=false;entities_=false;list_view_.scroll_offset=0;refresh(monitor);}
      if(hit==3&&l.refresh.contains(e.position)){list_view_.scroll_offset=0;refresh(monitor);entities_dirty_=true;}
      if(hit==4&&l.generation.contains(e.position)){generation_=true;assets_=false;entities_=false;list_view_.scroll_offset=0;}
      if(hit==5&&l.assets.contains(e.position)){assets_=true;generation_=false;entities_=false;list_view_.scroll_offset=0;}
      if(hit==6&&l.entities.contains(e.position)){entities_=true;events_=false;generation_=false;assets_=false;list_view_.scroll_offset=0;entities_dirty_=true;}
      if(hit>=100){
        const int row=hit-100;
        const int released=static_cast<int>((e.position.y-l.list.y+list_view_.scroll_offset)/list_view_.row_height);
        if(released==row&&entity_rows_rect(l).contains(e.position)&&row<static_cast<int>(entity_flat_.size())){
          const auto *node=entity_flat_[row].first;
          entity_selected_=node->id;entity_tree_.select(node->id);
          if(!node->children.empty())set_entity_expanded(*node,!node->expanded);
        }
      }
    }
    if(e.type==InputEventType::KeyPressed&&entities_&&!entities_dirty_){
      // SDL_Keycode arrows/Home/End/Return/Space drive the tree's
      // selection contract — the same semantics the editor's list gets.
      constexpr std::uint32_t kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      const auto row_of=[&](std::string_view id){
        for(std::size_t i=0;i<entity_flat_.size();++i)
          if(entity_flat_[i].first->id==id)return static_cast<int>(i);
        return -1;};
      const auto select_row=[&](int row){
        if(row<0||row>=static_cast<int>(entity_flat_.size()))return;
        entity_selected_=entity_flat_[row].first->id;
        entity_tree_.select(entity_selected_);
        list_view_.ensure_visible(static_cast<std::size_t>(row));snap_list();};
      if(e.key==kHome||e.key==kEnd){
        select_row(e.key==kHome?0:static_cast<int>(entity_flat_.size())-1);
      }else if(e.key==kDown||e.key==kUp){
        if(entity_tree_.selected()==nullptr)
          select_row(e.key==kDown?0:static_cast<int>(entity_flat_.size())-1);
        else{
          entity_tree_.move_selection(e.key==kDown?1:-1);
          entity_selected_=entity_tree_.selected_id();
          select_row(row_of(entity_selected_));
        }
      }else if(const int row=row_of(entity_selected_);row>=0){
        const auto *node=entity_flat_[row].first;
        if(e.key==kRight){
          if(!node->children.empty()){
            if(node->expanded)select_row(row+1);else set_entity_expanded(*node,true);
          }
        }else if(e.key==kLeft){
          if(!node->children.empty()&&node->expanded)set_entity_expanded(*node,false);
          else if(!node->parent_id.empty())select_row(row_of(node->parent_id));
        }else if((e.key==kReturn||e.key==kSpace)&&!node->children.empty())
          set_entity_expanded(*node,!node->expanded);
      }
      return true;
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
        const auto range=scroll_window(l,61.f,records.size(),7);
        for(auto i=range.first;i<range.last;++i){const auto&r=records[i];std::uint64_t size=0;for(const auto&c:r.chunks)size+=c.stored_bytes;const auto y=l.list.y+static_cast<float>(i)*61*s-list_view_.scroll_offset;
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
      // World store — the panel keeps one projected world and reconciles
      // it through sync_campaign_world on open/refresh, so surviving rows
      // hold stable EntityIds between refreshes (the incremental path's
      // first live consumer).
      const auto &projected=entity_world_;
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
      if(entities_dirty_){
        entity_parented_=0;
        if(!entities_world_built_){
          entity_world_=stellar::core::project_campaign_world(frame.runtime().world().campaign());
          entities_world_built_=true;entity_sync_.reset();
        }else entity_sync_=stellar::core::sync_campaign_world(entity_world_,frame.runtime().world().campaign());
        entity_bytes_=projected.estimated_memory_bytes();
        entity_total_=projected.size();
        // The projected hierarchy becomes a collapsible TreeModel —
        // the client's first tree consumer. Parents must precede
        // children (add resolves the link eagerly), so entities are
        // placed in passes; anything left over (a defensive case — the
        // projection never emits cycles) lands as a root.
        entity_tree_=stellar::engine::TreeModel{};
        const auto node_id=[](stellar::engine::EntityId e){
          return "e"+std::to_string(e.value());};
        const auto entity_line=[&](stellar::engine::EntityId e){
          std::string line=legacy_name(e);
          if(const auto detail=tag_detail(e);!detail.empty())line+="   ·   "+detail;
          return line;};
        const auto all=projected.entities();
        std::unordered_set<std::uint64_t> placed;placed.reserve(all.size());
        for(bool progress=true;placed.size()<all.size()&&progress;){
          progress=false;
          for(const auto e:all){
            if(placed.contains(e.value()))continue;
            const auto p=projected.parent(e);
            if(p&&!placed.contains(p->value()))continue;
            auto &node=entity_tree_.add(node_id(e),entity_line(e),p?node_id(*p):"");
            // Roots open by default; deeper levels follow the user's
            // toggles, which persist across sync refreshes.
            node.expanded=p?entity_expanded_.contains(node.id):!entity_collapsed_.contains(node.id);
            if(p)++entity_parented_;
            placed.insert(e.value());progress=true;
          }
        }
        for(const auto e:all)
          if(placed.insert(e.value()).second)
            entity_tree_.add(node_id(e),entity_line(e)).expanded=!entity_collapsed_.contains(node_id(e));
        entity_flat_=entity_tree_.flattened();
        // Selection is id-stable like the expansion sets — re-apply it;
        // a node the sync removed simply clears.
        entity_tree_.select(entity_selected_);
        entity_selected_=entity_tree_.selected_id();
        entities_dirty_=false;
      }
      label({l.list.x,l.list.y-31*s,l.list.width,27*s},std::to_string(entity_total_)+" projected entities · "+std::to_string(entity_parented_)+" parented · "+number(entity_bytes_/1024.)+" KiB estimated container footprint · "+
        (entity_sync_?"synced +"+std::to_string(entity_sync_->created)+" ~"+std::to_string(entity_sync_->updated)+" -"+std::to_string(entity_sync_->destroyed)+" ↻"+std::to_string(entity_sync_->reparented):"fresh projection")+" · read-only",native_menu_style::muted);
      // The list splits into the row scroll view (left) and a read-only
      // field dump of the selected entity (right) — the same
      // list+details idiom as the celestial index.
      const UiRect rows_rect=entity_rows_rect(l);
      const UiRect detail{l.list.x+rows_rect.width+14*s,l.list.y,l.list.width-rows_rect.width-14*s,l.list.height};
      const auto range=scroll_window(l,33.f,entity_flat_.size(),14);
      for(auto i=range.first;i<range.last;++i){
        const auto &[node,depth]=entity_flat_[i];
        const auto y=l.list.y+static_cast<float>(i)*33*s-list_view_.scroll_offset;
        const bool selected=node->id==entity_selected_;
        if(selected)out.overlay.emplace_back(FilledRectangle{{rows_rect.x,y,rows_rect.width,31*s},{24,64,88,230}});
        else if((i-range.first)%2==0)out.overlay.emplace_back(FilledRectangle{{rows_rect.x,y,rows_rect.width,31*s},{12,32,45,210}});
        const float indent=8*s+static_cast<float>(depth)*20*s;
        const std::string glyph=node->children.empty()?"· ":(node->expanded?"▾ ":"› ");
        label({rows_rect.x+indent,y+4*s,rows_rect.width-indent-8*s,25*s},glyph+node->label_key,selected?native_menu_style::cyan:native_menu_style::ink);
      }
      if(entity_flat_.empty())label(rows_rect,"The campaign projected no entities.");
      if(const auto sel=entity_for_node(entity_selected_);sel&&projected.alive(*sel)){
        const auto e=*sel;float y=detail.y;
        const auto line=[&](std::string value,Color color=native_menu_style::muted){label({detail.x,y,detail.width,24*s},std::move(value),color);y+=26*s;};
        line("SELECTED ENTITY",native_menu_style::cyan);
        line(legacy_name(e),native_menu_style::ink);
        line("entity index "+std::to_string(e.index)+" · generation "+std::to_string(e.generation));
        if(const auto p=projected.parent(e))line("parent · "+legacy_name(*p));
        line("children "+std::to_string(projected.children(e).size()));
        using namespace stellar::core;
        if(const auto*system_tag=projected.get<CampaignSystemTag>(e)){line("tag campaign.system",native_menu_style::cyan);line("id "+std::to_string(system_tag->id));line("x "+number(system_tag->x));line("y "+number(system_tag->y));}
        else if(const auto*body_tag=projected.get<CampaignBodyTag>(e)){line("tag campaign.body",native_menu_style::cyan);line("id "+std::to_string(body_tag->id));line("system "+std::to_string(body_tag->system_id));}
        else if(const auto*civ_tag=projected.get<CampaignCivilizationTag>(e)){line("tag campaign.civilization",native_menu_style::cyan);line("id "+std::to_string(civ_tag->id));line("home system "+std::to_string(civ_tag->home_system_id));}
        else if(const auto*colony_tag=projected.get<CampaignColonyTag>(e)){line("tag campaign.colony",native_menu_style::cyan);line("id "+std::to_string(colony_tag->id));line("civ "+std::to_string(colony_tag->civilization_id));line("system "+std::to_string(colony_tag->system_id));}
        else if(const auto*fleet_tag=projected.get<CampaignFleetTag>(e)){line("tag campaign.fleet",native_menu_style::cyan);line("id "+std::to_string(fleet_tag->id));line("civ "+std::to_string(fleet_tag->civilization_id));}
        else if(const auto*economy_tag=projected.get<CampaignEconomyTag>(e)){line("tag campaign.economy",native_menu_style::cyan);line("civ "+std::to_string(economy_tag->civilization_id));}
        else if(const auto*tech_tag=projected.get<CampaignTechnologyTag>(e)){line("tag campaign.technology",native_menu_style::cyan);line("civ "+std::to_string(tech_tag->civilization_id));}
        else if(const auto*construction_tag=projected.get<CampaignConstructionTag>(e)){line("tag campaign.construction",native_menu_style::cyan);line("civ "+std::to_string(construction_tag->civilization_id));}
        else if(const auto*shipyard_tag=projected.get<CampaignShipyardTag>(e)){line("tag campaign.shipyard",native_menu_style::cyan);line("civ "+std::to_string(shipyard_tag->civilization_id));}
      }else label(detail,"Select a row — the projected entity's index, refs and tag fields list here.",native_menu_style::muted);
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
      const auto range=scroll_window(l,33.f,rows.size(),14);
      for(auto i=range.first;i<range.last;++i){
        const auto &p=rows[i];const auto y=l.list.y+static_cast<float>(i)*33*s-list_view_.scroll_offset;
        if((i-range.first)%2==0)out.overlay.emplace_back(FilledRectangle{{l.list.x,y,l.list.width,31*s},{12,32,45,210}});
        label({l.list.x+8*s,y+4*s,400*s,25*s},p.phase);
        label({l.list.x+430*s,y+4*s,140*s,25*s},std::to_string(p.n));
        label({l.list.x+600*s,y+4*s,170*s,25*s},p.n?number(static_cast<double>(p.total)/p.n/1e6):"Unmeasured");
        label({l.list.x+810*s,y+4*s,190*s,25*s},p.n?number(p.maximum/1e6):"Unmeasured");
      }
    }else{
      label({l.list.x,l.list.y-31*s,l.list.width,27*s},std::to_string(snapshot_.size())+" retained events · newest first · snapshot captured on refresh",native_menu_style::muted);
      const auto range=scroll_window(l,57.f,snapshot_.size(),8);
      for(auto i=range.first;i<range.last;++i){
        const auto &r=snapshot_[i];const auto y=l.list.y+static_cast<float>(i)*57*s-list_view_.scroll_offset;
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
  // In the entities view the list splits: rows left, selected-entity
  // detail right — row hit-testing bounds to the rows region.
  static UiRect entity_rows_rect(const Layout &l){return {l.list.x,l.list.y,l.list.width*.62f,l.list.height};}
  // Configures the shared VirtualizedList for the active view, re-clamps
  // the offset (collapses/refreshes shrink content), publishes the row
  // count for handle(), and returns the visible range capped at the
  // view's historical row cap so overscan never paints past the list.
  stellar::engine::VirtualizedList::Range scroll_window(const Layout &l,float stride,std::size_t rows,std::size_t cap)const{
    list_view_.row_height=stride*l.scale;list_view_.viewport_height=l.list.height;list_view_.row_count=rows;
    list_view_.scroll_to(list_view_.scroll_offset);snap_list();list_rows_=rows;
    auto range=list_view_.visible_range();range.last=std::min(range.last,range.first+cap);
    return range;
  }
  // This panel scrolls whole rows (rows always start fully visible at
  // the list top) — quantize the pixel offset to the row stride.
  void snap_list()const{if(list_view_.row_height>0)list_view_.scroll_offset=std::floor(list_view_.scroll_offset/list_view_.row_height)*list_view_.row_height;}
  // Node ids encode the EntityId ("e"+value()); decode back for the
  // detail pane.
  static std::optional<stellar::engine::EntityId> entity_for_node(std::string_view id){
    if(id.size()<2||id.front()!='e')return std::nullopt;
    std::uint64_t value{};
    const auto r=std::from_chars(id.data()+1,id.data()+id.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=id.data()+id.size())return std::nullopt;
    return stellar::engine::EntityId{static_cast<std::uint32_t>(value&0xffffffffu),static_cast<std::uint32_t>(value>>32)};
  }
  bool generation_{},assets_{},entities_{};
  mutable bool entities_dirty_{true},entities_world_built_{};
  mutable stellar::engine::World entity_world_;
  mutable std::optional<stellar::core::CampaignWorldProjectionSync> entity_sync_;
  mutable stellar::engine::TreeModel entity_tree_;
  mutable std::vector<std::pair<const stellar::engine::TreeModel::Node *,int>> entity_flat_;
  mutable std::unordered_set<std::string> entity_expanded_,entity_collapsed_;
  mutable std::string entity_selected_;
  mutable int entity_parented_{};
  mutable std::size_t entity_bytes_{},entity_total_{};
  void set_entity_expanded(const stellar::engine::TreeModel::Node &node,bool expand){
    entity_tree_.set_expanded(node.id,expand);
    // Roots persist collapses; deeper nodes persist expansions
    // (roots default open, deeper levels default closed).
    if(node.parent_id.empty()){
      if(expand)entity_collapsed_.erase(node.id);else entity_collapsed_.insert(node.id);
    }else{
      if(expand)entity_expanded_.insert(node.id);else entity_expanded_.erase(node.id);
    }
    entity_flat_=entity_tree_.flattened();
    // Collapsing an ancestor hides a selected descendant — fall back to
    // the toggled row so the selection stays on a visible node.
    if(!entity_selected_.empty()){
      bool visible=false;
      for(const auto &r:entity_flat_)if(r.first->id==entity_selected_){visible=true;break;}
      if(!visible){entity_selected_=node.id;entity_tree_.select(node.id);}
    }
  }
  void refresh(const stellar::app_diagnostics::CampaignDiagnosticMonitor &monitor){
    snapshot_.clear();for(auto i=monitor.history().records().rbegin();i!=monitor.history().records().rend();++i)snapshot_.push_back(i->record);
  }
  static std::string number(double value){std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<value;return out.str();}
  std::vector<stellar::engine::DiagnosticRecord> snapshot_;stellar::native_ui::Dropdown dropdown_;
  bool visible_{},events_{};int pressed_{-1};Point pointer_{};
  mutable stellar::engine::VirtualizedList list_view_;mutable std::size_t list_rows_{};
};
}
