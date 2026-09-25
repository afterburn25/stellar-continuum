#include "native_logistics_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_theme.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace stellar::native_logistics {
using namespace stellar::native_map;
namespace theme = stellar::native_ui;
using stellar::core::SupplyCondition;
namespace {
constexpr Color ink = theme::color::text_primary;
constexpr Color muted = theme::color::text_muted;
constexpr Color cyan = theme::color::selected;
constexpr Color amber = theme::color::caution;
std::string number(double value) { if(value>0.&&value<.01)return "<0.01"; std::ostringstream out; out<<std::fixed<<std::setprecision(2)<<value; return out.str(); }
float text_height(const std::function<TextExtent(const Text&)>& measure,
                  const std::string& text,float width,int font) {
  if(measure)return static_cast<float>(std::max(1,measure(Text{{},text,ink,font,width}).height));
  const float columns=std::max(1.f,width/(static_cast<float>(font)*.6f));
  return std::max(1.f,std::ceil(static_cast<float>(text.size())/columns))*static_cast<float>(font+5);
}
UiRect intersect(UiRect a,UiRect b) {
  const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);
  return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),
              std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};
}
void label(DrawList& out,UiRect box,std::string value,int font,Color color,UiRect clip) {
  const auto visible=intersect(box,clip);
  if(visible.width>0.f&&visible.height>0.f)
    out.overlay.emplace_back(Text{{box.x,box.y},std::move(value),color,font,box.width,visible});
}
bool pointer_event(InputEventType type) {
  return type==InputEventType::LeftPressed||type==InputEventType::LeftReleased||
    type==InputEventType::RightPressed||type==InputEventType::RightReleased||
    type==InputEventType::PointerMove||type==InputEventType::Wheel;
}
}
SupplyLayout SupplyLayout::for_viewport(int width,int height) {
  const float s=NativeUiLayout::for_viewport(width,height).scale;
  const auto top=native_workspace_top(width,height);
  const UiRect panel{80.f*s,top,std::max(200.f,static_cast<float>(width)-98.f*s),
                      std::max(180.f,static_cast<float>(height)-top-34.f*s)};
  return {panel,{panel.x+18.f*s,panel.y+244.f*s,panel.width-44.f*s,std::max(0.f,panel.height-260.f*s)},
    {panel.x+panel.width-46.f*s,panel.y+14.f*s,30.f*s,30.f*s},
    {panel.x+panel.width-188.f*s,panel.y+14.f*s,128.f*s,30.f*s},s};
}
std::string SupplyWorkspace::tr(std::string_view key,
                                std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string SupplyWorkspace::trf(
    std::string_view key, std::initializer_list<std::string> args,
    std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale_->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

void SupplyWorkspace::clear_rows() noexcept { rows_={}; }
void SupplyWorkspace::open() noexcept { visible_=true;scroll_={};owned_=false;focus_=-1;clear_rows(); }
void SupplyWorkspace::close() noexcept { visible_=false;owned_=false;focus_=-1;clear_rows(); }
std::string SupplyWorkspace::focused_label(const View &view) const {
  if (focus_ < 0) return {};
  if (focus_ == 0)
    return tr(view.state == LoadState::Failed ? "SUPPLY_RETRY" : "SUPPLY_REFRESH",
              view.state == LoadState::Failed ? "Retry" : "Refresh");
  return tr("SUPPLY_CLOSE", "Close supply network");
}
std::optional<UiRect> SupplyWorkspace::focused_bounds(int width,
                                                      int height) const {
  if (focus_ < 0) return std::nullopt;
  const auto layout = SupplyLayout::for_viewport(width, height);
  return focus_ == 0 ? std::optional<UiRect>{layout.refresh}
                     : std::optional<UiRect>{layout.close};
}
void SupplyWorkspace::set_text_measurer(std::function<TextExtent(const Text&)> value) {
  measure_=std::move(value);++measurer_revision_;clear_rows();
}
const SupplyWorkspace::CachedRows &SupplyWorkspace::rows_for(
    const View& view,const SupplyLayout& layout,int width,int height) const {
  if(rows_.valid&&rows_.viewport_width==width&&rows_.viewport_height==height&&
     rows_.measurer_revision==measurer_revision_&&rows_.nodes==view.nodes&&
     rows_.links==view.links&&rows_.external==view.external)return rows_;
  rows_={};rows_.viewport_width=width;rows_.viewport_height=height;
  rows_.measurer_revision=measurer_revision_;rows_.nodes=view.nodes;
  rows_.links=view.links;rows_.external=view.external;
  rows_.rows.reserve(view.nodes.size()+view.links.size()+view.external.size()+2);
  const auto s=layout.scale;const int font=static_cast<int>(15.f*s);
  for(std::size_t index=0;index<view.nodes.size();++index){
    const auto& node=view.nodes[index];
    const auto name=text_height(measure_,node.name,layout.body.width*.30f-20.f*s,font);
    const auto kind=text_height(measure_,node.kind_label,layout.body.width*.30f-20.f*s,font-2);
    const auto state=text_height(measure_,node.status,layout.body.width*.22f-16.f*s,font);
    const auto row_height=std::max({64.f*s,name+kind+22.f*s,state+20.f*s});
    rows_.rows.push_back({index,rows_.height,row_height,name,0});
    rows_.height+=row_height+6.f*s;
  }
  if(!view.links.empty()){
    rows_.rows.push_back({0,rows_.height+8.f*s,46.f*s,0.f,1});
    rows_.height+=54.f*s+6.f*s;
    for(std::size_t index=0;index<view.links.size();++index){
      const auto& link=view.links[index];
      const auto name=text_height(measure_,link.from+(link.bidirectional?" <-> ":" -> ")+link.to,layout.body.width*.30f-20.f*s,font);
      const auto state=text_height(measure_,link.status,layout.body.width*.22f-16.f*s,font);
      const auto row_height=std::max({44.f*s,name+20.f*s,state+20.f*s});
      rows_.rows.push_back({index,rows_.height,row_height,name,2});
      rows_.height+=row_height+6.f*s;
    }
  }
  if(!view.external.empty()){
    rows_.rows.push_back({0,rows_.height+8.f*s,46.f*s,0.f,3});
    rows_.height+=54.f*s+6.f*s;
    for(std::size_t index=0;index<view.external.size();++index){
      const auto& external=view.external[index];
      const auto name=text_height(measure_,external.name,layout.body.width*.30f-20.f*s,font);
      const auto state=text_height(measure_,external.status,layout.body.width*.22f-16.f*s,font);
      const auto row_height=std::max({64.f*s,name+26.f*s,state+20.f*s});
      rows_.rows.push_back({index,rows_.height,row_height,name,4});
      rows_.height+=row_height+6.f*s;
    }
  }
  rows_.valid=true;return rows_;
}
SupplyCommand SupplyWorkspace::handle(const InputEvent& event,const View& view,int width,int height) {
  if(!visible_)return {};
  pointer_=event.position;
  const auto layout=SupplyLayout::for_viewport(width,height);
  if(event.type==InputEventType::PointerCancelled){owned_=false;return {true,false};}
  if(event.type==InputEventType::EscapePressed){close();return {true,false};}
  if(event.type==InputEventType::KeyPressed&&event.key){
    // SDL_Keycode: Tab/arrows move the ring over [refresh, close];
    // Return/Space replay the click gesture through the same dispatch.
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const bool fwd=(event.key==kTab&&!event.shift)||event.key==kRight||event.key==kDown;
    const bool bwd=(event.key==kTab&&event.shift)||event.key==kLeft||event.key==kUp;
    if(event.key==kHome||event.key==kEnd){focus_=event.key==kHome?0:1;return {true,false};}
    if(fwd||bwd){focus_=focus_<0?(bwd?1:0):(focus_+(bwd?-1:1)+2)%2;return {true,false};}
    if((event.key==kReturn||event.key==kSpace)&&focus_>=0){
      const auto&rect=focus_==0?layout.refresh:layout.close;
      InputEvent press{InputEventType::LeftPressed},release{InputEventType::LeftReleased};
      press.position=release.position={rect.x+rect.width*.5f,rect.y+rect.height*.5f};
      const int keep=focus_;auto command=handle(press,view,width,height);
      static_cast<void>(handle(release,view,width,height));
      if(visible_)focus_=keep;return command;
    }
    return {}; // Non-modal: unhandled keys pass through to global shortcuts.
  }
  if(!pointer_event(event.type))return {};
  if(event.type==InputEventType::LeftReleased||event.type==InputEventType::RightReleased){
    const bool captured=owned_||layout.panel.contains(event.position);owned_=false;return {captured,false};
  }
  if(owned_)return {true,false};
  if(!layout.panel.contains(event.position))return {};
  if(event.type==InputEventType::LeftPressed){
    if(layout.close.contains(event.position)){close();return {true,false};}
    focus_=-1;
    owned_=true;
    return {true,layout.refresh.contains(event.position)};
  }
  if(event.type==InputEventType::RightPressed)owned_=true;
  if(event.type==InputEventType::Wheel&&layout.body.contains(event.position)) {
    const auto& rows=rows_for(view,layout,width,height);
    scroll_.sync(rows.height,layout.body.height);
    scroll_.scroll_by(-event.wheel_y*55.f*layout.scale);
  }
  return {true,false};
}
void SupplyWorkspace::render(DrawList& out,const View& view,int width,int height) const {
  if(!visible_)return;
  const auto layout=SupplyLayout::for_viewport(width,height);
  const auto p=layout.panel;const float s=layout.scale;
  const int font=static_cast<int>(15.f*s);
  theme::panel(out,p);
  label(out,{p.x+18.f*s,p.y+14.f*s,p.width-225.f*s,32.f*s},tr("SUPPLY_TITLE","SUPPLY NETWORK"),static_cast<int>(24.f*s),ink,p);
  theme::button(out,layout.refresh,
      tr(view.state==LoadState::Failed?"SUPPLY_RETRY":"SUPPLY_REFRESH",view.state==LoadState::Failed?"RETRY":"REFRESH"),
      pointer_,font,theme::Tone::Selected);
  theme::button(out,layout.close,"X",pointer_,font);
  const bool ready=view.state==LoadState::Ready;
  label(out,{p.x+18.f*s,p.y+54.f*s,p.width-36.f*s,28.f*s},
      ready?trf("SUPPLY_SUBTITLE",{view.system_name,std::to_string(view.corridor_count)},
                "{0}  /  HOME SYSTEM  /  {1} TRANSPORT LINKS")
           :tr("SUPPLY_UNAVAILABLE","HOME SYSTEM SUPPLY UNAVAILABLE"),font,cyan,p);
  label(out,{p.x+18.f*s,p.y+85.f*s,p.width-36.f*s,45.f*s},view.message,font,ready?muted:amber,p);
  if(focus_>=0)theme::focus_ring(out,focus_==0?layout.refresh:layout.close);
  if(!ready)return; // Never render stale totals or healthy zeroes after a failure.
  const std::array<const char*,4> metric_keys{"SUPPLY_METRIC_AVAILABLE","SUPPLY_METRIC_DEMAND","SUPPLY_METRIC_DELIVERED","SUPPLY_METRIC_SHORTFALL"};
  const std::array<std::string,4> metric_fallbacks{"AVAILABLE","DEMAND","DELIVERED","SHORTFALL"};
  const std::array<std::string,4> names{tr(metric_keys[0],metric_fallbacks[0]),tr(metric_keys[1],metric_fallbacks[1]),tr(metric_keys[2],metric_fallbacks[2]),tr(metric_keys[3],metric_fallbacks[3])};
  const std::array<double,4> values{view.supply_per_day,view.demand_per_day,view.delivered_per_day,view.shortfall_per_day};
  const float metric_width=(p.width-54.f*s)/4.f;
  for(std::size_t index=0;index<4;++index){
    const UiRect box{p.x+18.f*s+static_cast<float>(index)*(metric_width+6.f*s),p.y+136.f*s,metric_width,60.f*s};
    const bool shortfall=index==3&&values[index]>.00001;
    theme::metric_tile(out,box,names[index],
        trf("SUPPLY_PER_DAY",{number(values[index])},"{0} / day"),font-2,font+3,
        shortfall?theme::Tone::Caution:theme::Tone::Neutral);
  }
  const auto b=layout.body;
  const std::array<float,5> columns{0.f,.30f,.52f,.68f,.84f};
  const std::array<float,5> spans{.30f,.22f,.16f,.16f,.16f};
  const std::array<const char*,5> heading_keys{"SUPPLY_COL_LOCATION","SUPPLY_COL_STATUS","SUPPLY_COL_OFFERED","SUPPLY_COL_DEMAND","SUPPLY_COL_DELIVERED"};
  const std::array<const char*,5> heading_fallbacks{"LOCATION / FACILITY","STATUS","OFFERED / DAY","DEMAND / DAY","DELIVERED / DAY"};
  for(std::size_t i=0;i<columns.size();++i)
    label(out,{b.x+b.width*columns[i]+8.f*s,p.y+215.f*s,b.width*spans[i]-16.f*s,23.f*s},tr(heading_keys[i],heading_fallbacks[i]),font-2,muted,p);
  theme::fill(out,{b.x,b.y-4.f*s,b.width,1.f},theme::color::keyline);
  const auto& rows=rows_for(view,layout,width,height);
  scroll_.sync(rows.height,b.height);
  for(const auto& row:rows.rows){
    const UiRect box{b.x,b.y+row.y-scroll_.scroll_offset,b.width,row.height};
    const auto visible=intersect(box,b);if(visible.height<=0.f)continue;
    if(row.kind==1){
      label(out,{box.x,box.y+6.f*s,box.width,20.f*s},
          tr("SUPPLY_CORRIDORS_TITLE","FREIGHT CORRIDORS"),font,
          theme::color::keyline_strong,b);
      const std::array<const char*,4> corridor_keys{"SUPPLY_COL_STATUS","SUPPLY_COL_CAPACITY","SUPPLY_COL_USED","SUPPLY_COL_TRANSIT"};
      const std::array<const char*,4> corridor_fallbacks{"STATUS","CAPACITY / DAY","USED / DAY","TRANSIT"};
      for(std::size_t i=0;i<corridor_keys.size();++i)
        label(out,{box.x+b.width*columns[i+1]+8.f*s,box.y+24.f*s,b.width*spans[i+1]-16.f*s,18.f*s},
              tr(corridor_keys[i],corridor_fallbacks[i]),font-2,muted,b);
      if(const auto rule=theme::clipped({box.x,box.y+row.height-3.f*s,box.width,1.f},b))
        theme::fill(out,*rule,theme::color::keyline);
      continue;
    }
    if(row.kind==3){
      label(out,{box.x,box.y+6.f*s,box.width,20.f*s},
          tr("SUPPLY_EXTERNAL_TITLE","INTERSTELLAR COVERAGE"),font,
          theme::color::keyline_strong,b);
      if(view.support_gap_per_day>.00001)
        label(out,{box.x+b.width*.44f,box.y+6.f*s,b.width*.56f,20.f*s},
              trf("SUPPLY_SUPPORT_GAP",{number(view.support_gap_per_day)},
                  "UNREPRESENTED INTERSTELLAR DEMAND  {0} / DAY"),
              font-2,amber,b);
      const std::array<const char*,4> external_keys{"SUPPLY_COL_STATUS","SUPPLY_COL_LOCAL","SUPPLY_COL_DEMAND","SUPPLY_COL_IMPORT"};
      const std::array<const char*,4> external_fallbacks{"STATUS","LOCAL / DAY","DEMAND / DAY","IMPORT / DAY"};
      for(std::size_t i=0;i<external_keys.size();++i)
        label(out,{box.x+b.width*columns[i+1]+8.f*s,box.y+24.f*s,b.width*spans[i+1]-16.f*s,18.f*s},
              tr(external_keys[i],external_fallbacks[i]),font-2,muted,b);
      if(const auto rule=theme::clipped({box.x,box.y+row.height-3.f*s,box.width,1.f},b))
        theme::fill(out,*rule,theme::color::keyline);
      continue;
    }
    if(row.kind==4){
      theme::fill(out,visible,theme::color::surface_secondary);
      const auto& external=view.external[row.index];
      const Color condition_tone=external.condition==SupplyCondition::Critical?theme::color::danger
          :external.condition==SupplyCondition::Strained?amber
          :external.corridor?cyan:muted;
      label(out,{box.x+8.f*s,box.y+10.f*s,b.width*.30f-20.f*s,row.name_height},
            external.name,font,ink,b);
      label(out,{box.x+8.f*s,box.y+row.name_height+14.f*s,b.width*.30f-20.f*s,row.height-row.name_height-14.f*s},
            trf("SUPPLY_COLONY_COUNT",{std::to_string(external.colony_count)},"{0} colonies"),font-2,muted,b);
      const std::array<std::string,4> values_text{external.status,
          number(external.capacity_per_day),number(external.demand_per_day),
          number(external.import_per_day)};
      for(std::size_t i=1;i<columns.size();++i)
        label(out,{box.x+b.width*columns[i]+8.f*s,box.y+12.f*s,b.width*spans[i]-16.f*s,row.height-20.f*s},values_text[i-1],font,
              i==1?condition_tone:i==4&&external.import_per_day>.00001&&!external.corridor?amber:ink,b);
      continue;
    }
    theme::fill(out,visible,theme::color::surface_secondary);
    if(row.kind==2){
      const auto& link=view.links[row.index];
      const Color status_tone=!link.enabled?muted
          :link.capacity_per_day<=.0001?(link.used_per_day>.0001?amber:muted)
          :link.used_per_day>=link.capacity_per_day-.0001?amber
          :link.used_per_day>=link.capacity_per_day*.75?amber
          :link.used_per_day>.0001?cyan:muted;
      label(out,{box.x+8.f*s,box.y+10.f*s,b.width*.30f-20.f*s,row.name_height},
            link.from+(link.bidirectional?" <-> ":" -> ")+link.to,font,ink,b);
      const std::array<std::string,4> values_text{link.status,
          number(link.capacity_per_day),number(link.used_per_day),
          trf("SUPPLY_TRANSIT_DAYS",{number(link.transit_days)},"{0} d")};
      for(std::size_t i=1;i<columns.size();++i)
        label(out,{box.x+b.width*columns[i]+8.f*s,box.y+12.f*s,b.width*spans[i]-16.f*s,row.height-20.f*s},values_text[i-1],font,
              i==1?status_tone:ink,b);
      continue;
    }
    const auto& n=view.nodes[row.index];
    label(out,{box.x+8.f*s,box.y+10.f*s,b.width*.30f-20.f*s,row.name_height},n.name,font,ink,b);
    label(out,{box.x+8.f*s,box.y+row.name_height+14.f*s,b.width*.30f-20.f*s,row.height-row.name_height-14.f*s},n.kind_label,font-2,muted,b);
    const std::array<std::string,4> values_text{n.status,number(n.supply_per_day),number(n.demand_per_day),number(n.delivered_per_day)};
    for(std::size_t i=1;i<columns.size();++i)
      label(out,{box.x+b.width*columns[i]+8.f*s,box.y+12.f*s,b.width*spans[i]-16.f*s,row.height-20.f*s},values_text[i-1],font,
            i==1&&n.delivered_per_day+.00001<n.demand_per_day?amber:ink,b);
  }
  if(view.nodes.empty())
    theme::empty_state(out,b,tr("SUPPLY_EMPTY","No owned supply locations in the home system."),{},font);
  theme::scrollbar(out,{b.x+b.width+7.f*s,b.y,3.f*s,b.height},scroll_,24.f*s);
}
} // namespace stellar::native_logistics
