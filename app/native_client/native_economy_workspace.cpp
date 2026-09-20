#include "native_economy_workspace.hpp"

#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace stellar::native_economy {
namespace {
using namespace stellar::native_map;
constexpr Color ink{231, 243, 252, 255}, muted{157, 184, 209, 255};
constexpr Color accent{123, 229, 199, 255}, gold{238, 196, 111, 255};
constexpr Color income{145, 227, 176, 255}, warning{247, 171, 126, 255};
constexpr Color inset{10, 29, 48, 246}, row{12, 34, 55, 248};

std::optional<UiRect> intersect(UiRect a, UiRect b) {
  const auto x=std::max(a.x,b.x), y=std::max(a.y,b.y);
  const auto r=std::min(a.x+a.width,b.x+b.width), bot=std::min(a.y+a.height,b.y+b.height);
  if(r<=x||bot<=y) return std::nullopt;
  return UiRect{x,y,r-x,bot-y};
}
void fill(DrawList& out, UiRect box, Color color) { out.overlay.emplace_back(FilledRectangle{box,color}); }
void text(DrawList& out, UiRect box, UiRect clip, std::string value, Color color, int font,
          TextAlign align=TextAlign::Left) {
  if(const auto visible=intersect(box,clip)) {
    const auto x=align==TextAlign::Right ? box.x+box.width : align==TextAlign::Center ? box.x+box.width*.5f : box.x;
    out.overlay.emplace_back(Text{{x,box.y},std::move(value),color,font,box.width,*visible,align});
  }
}
float measured_height(const NativeEconomyWorkspace::TextMeasurer& measure, const std::string& value,
                      float width, int font, float scale) {
  if(measure) {
    const auto extent=measure(Text{{},value,ink,font,width});
    return std::max(font*1.f, static_cast<float>(std::max(0,extent.height)));
  }
  const auto columns=std::max(1.f,width/std::max(1.f,font*.56f));
  float lines=1.f, line=0.f;
  for(unsigned char c : value) { if(c=='\n') {++lines;line=0;} else if(++line>columns) {++lines;line=1;} }
  return lines*(font+3.f*scale);
}
std::string signature(const NativeEconomyView& v, const std::string& notice) {
  std::ostringstream out; out << static_cast<int>(v.state) << '|' << v.message << '|' << v.diagnostic << '|' << v.treasury_status << '|' << v.priority_status << '|' << notice;
  for(const auto& c:v.cards) out << '|' << c.label << ':' << c.value;
  for(const auto& r:v.income_rows) out << '|' << r.label << ':' << r.value << r.suffix;
  for(const auto& r:v.cost_rows) out << '|' << r.label << ':' << r.value << r.suffix;
  return out.str();
}
bool pointer_event(InputEventType t) { return t==InputEventType::LeftPressed||t==InputEventType::LeftReleased||t==InputEventType::RightPressed||t==InputEventType::RightReleased||t==InputEventType::PointerMove||t==InputEventType::Wheel; }
} // namespace

EconomyLayout EconomyLayout::for_viewport(int width,int height) noexcept {
  const auto w=static_cast<float>(std::max(width,1)), h=static_cast<float>(std::max(height,1));
  const auto requested=std::max(1.f,h/900.f);
  const auto fit=std::max(.72f,std::min(w/1280.f,h/720.f));
  const auto s=std::min(requested,fit);
  const float x=native_navigation_content_left*s, top=native_workspace_top(width,height), margin=14.f*s;
  const UiRect panel{x,top,std::max(180.f,w-x-margin),std::max(180.f,h-top-margin)};
  const float pad=14.f*s;
  EconomyLayout result; result.scale=s;
  result.heading_font_pixels=std::max(14,static_cast<int>(23*s)); result.body_font_pixels=std::max(11,static_cast<int>(15*s)); result.small_font_pixels=std::max(9,static_cast<int>(12*s));
  result.panel=panel; result.header={panel.x+pad,panel.y+pad,panel.width-2*pad,31*s};
  result.close={panel.x+panel.width-pad-30*s,panel.y+pad,30*s,28*s};
  result.refresh={result.close.x-100*s,panel.y+pad,90*s,28*s};
  result.notice={panel.x+pad,panel.y+52*s,panel.width-2*pad,34*s};
  const float gap=7*s, buttons_y=panel.y+88*s, buttons_w=(panel.width-2*pad-2*gap)/3.f;
  for(int i=0;i<3;++i) result.priority_buttons[i]={panel.x+pad+i*(buttons_w+gap),buttons_y,buttons_w,30*s};
  result.body={panel.x+pad,panel.y+126*s,panel.width-2*pad,std::max(1.f,panel.height-140*s)};
  return result;
}

void NativeEconomyWorkspace::reset_gesture() noexcept { pointer_owned_=dragging_=false; pressed_=PressTarget::None; }
void NativeEconomyWorkspace::open() noexcept { visible_=true; scroll_=0; reset_gesture(); }
void NativeEconomyWorkspace::close() noexcept { visible_=false; reset_gesture(); }
void NativeEconomyWorkspace::clear() noexcept { close(); notice_.clear(); cache_={}; observed_generation_=observed_revision_=0; observed_observer_=0; }
void NativeEconomyWorkspace::set_text_measurer(TextMeasurer measure) { measure_=std::move(measure); ++measure_revision_; cache_.valid=false; }
void NativeEconomyWorkspace::set_notice(std::string notice) { notice_=std::move(notice); cache_.valid=false; }

const NativeEconomyWorkspace::Cache& NativeEconomyWorkspace::cache_for(const NativeEconomyView& view,const EconomyLayout& layout,int width,int height) const {
  const auto sig=signature(view,notice_);
  if(cache_.valid&&cache_.width==width&&cache_.height==height&&cache_.generation==view.campaign_generation&&cache_.revision==view.revision&&cache_.measure_revision==measure_revision_&&cache_.signature==sig) return cache_;
  cache_={}; cache_.width=width;cache_.height=height;cache_.generation=view.campaign_generation;cache_.revision=view.revision;cache_.measure_revision=measure_revision_;cache_.signature=sig;
  const auto add=[&](std::string left,std::string right,bool inc,bool warn_row=false) {
    const auto left_h=measured_height(measure_,left,layout.body.width*.36f-16.f*layout.scale,layout.small_font_pixels,layout.scale);
    const auto right_h=measured_height(measure_,right,layout.body.width*.60f-16.f*layout.scale,layout.body_font_pixels,layout.scale);
    const auto row_h=std::max(32.f*layout.scale,std::max(left_h,right_h)+14.f*layout.scale);
    cache_.rows.push_back({std::move(left),std::move(right),inc,warn_row,false,0,cache_.content_height,row_h}); cache_.content_height+=row_h+6.f*layout.scale;
  };
  if(view.state==EconomyState::Ready) {
    const auto tile_gap=6.f*layout.scale;
    const auto tile_width=(layout.body.width-2.f*tile_gap)/3.f-16.f*layout.scale;
    for(int row_index=0;row_index<2;++row_index) {
      float tile_height=50.f*layout.scale;
      for(int col=0;col<3;++col) {
        const auto& card=view.cards[static_cast<std::size_t>(row_index*3+col)];
        tile_height=std::max(tile_height,28.f*layout.scale+
            measured_height(measure_,card.value,tile_width,layout.body_font_pixels,layout.scale));
      }
      for(int col=0;col<3;++col) {
        const auto& card=view.cards[static_cast<std::size_t>(row_index*3+col)];
        cache_.rows.push_back({card.label,card.value,!card.warning,card.warning,true,col,
            cache_.content_height,tile_height});
      }
      cache_.content_height+=tile_height+tile_gap;
    }
    cache_.content_height+=2.f*layout.scale;
    add("TREASURY HEALTH",view.treasury_status,view.treasury_healthy,!view.treasury_healthy);
    add("INDUSTRIAL PRIORITY",view.priority_status,true);
    add("INCOME", "", true);
    for(const auto& r:view.income_rows) add(r.label,r.value+r.suffix,true);
    add("OPERATING COSTS", "", false,true);
    for(const auto& r:view.cost_rows) add(r.label,r.value+r.suffix,false,true);
    add("TREASURY GUIDANCE", "Construction and ship orders are capital costs. Active research and operations are daily commitments.",true);
  } else {
    const auto failed=view.state==EconomyState::Failed;
    add("TREASURY DATA UNAVAILABLE",failed
        ? "Treasury data could not be refreshed. Retry to request a new snapshot."
        : "Treasury data is unavailable for the current observer. Retry after a campaign is available.",false,true);
    // Diagnostics belong in the application's log. A player-facing workspace
    // must not expose exception text or a serialized campaign/player object.
  }
  if(!notice_.empty()) add("ECONOMY NOTICE",notice_,true);
  if(!cache_.rows.empty()) cache_.content_height-=6.f*layout.scale;
  cache_.valid=true; return cache_;
}

NativeEconomyWorkspace::PressTarget NativeEconomyWorkspace::hit(Point point,const EconomyLayout& layout) const noexcept {
  if(layout.close.contains(point)) return PressTarget::Close;
  if(layout.refresh.contains(point)) return PressTarget::Refresh;
  for(int i=0;i<3;++i) if(layout.priority_buttons[i].contains(point)) return static_cast<PressTarget>(static_cast<int>(PressTarget::Priority0)+i);
  return layout.body.contains(point)?PressTarget::Body:PressTarget::None;
}
core::IndustryPriority NativeEconomyWorkspace::priority_for(PressTarget target) noexcept { return static_cast<core::IndustryPriority>(static_cast<int>(target)-static_cast<int>(PressTarget::Priority0)); }

EconomyCommand NativeEconomyWorkspace::handle(const InputEvent& event,const NativeEconomyView& view,int width,int height) {
  EconomyCommand command; if(!visible_) return command;
  const auto layout=EconomyLayout::for_viewport(width,height);
  if(view.campaign_generation!=observed_generation_||view.revision!=observed_revision_||view.player_civilization_id!=observed_observer_) { reset_gesture(); observed_generation_=view.campaign_generation;observed_revision_=view.revision;observed_observer_=view.player_civilization_id; }
  command.view_revision=view.revision;
  if(event.type==InputEventType::EscapePressed) { close(); command.kind=EconomyCommandKind::Close;command.captured=true; return command; }
  if(event.type==InputEventType::PointerCancelled) { const bool captured=pointer_owned_; reset_gesture(); command.captured=captured; return command; }
  if(!pointer_event(event.type)) return command;
  const auto& cached=cache_for(view,layout,width,height);
  const auto maximum=std::max(0.f,cached.content_height-layout.body.height);
  scroll_=std::clamp(scroll_,0.f,maximum);
  if(event.type==InputEventType::Wheel) { if(layout.panel.contains(event.position)) { if(layout.body.contains(event.position)) scroll_=std::clamp(scroll_-event.wheel_y*52.f*layout.scale,0.f,maximum); command.captured=true; } return command; }
  if(event.type==InputEventType::LeftPressed||event.type==InputEventType::RightPressed) {
    if(!layout.panel.contains(event.position)) return command;
    pointer_owned_=true; press_point_=event.position; press_scroll_=scroll_; pressed_=event.type==InputEventType::LeftPressed?hit(event.position,layout):PressTarget::Body; command.captured=true; return command;
  }
  if(event.type==InputEventType::PointerMove) {
    if(!pointer_owned_) return command; command.captured=true;
    if(pressed_==PressTarget::Body) { const auto distance=event.position.y-press_point_.y; if(std::abs(distance)>2.f*layout.scale) dragging_=true; if(dragging_) scroll_=std::clamp(press_scroll_-distance,0.f,maximum); }
    return command;
  }
  if(event.type==InputEventType::LeftReleased||event.type==InputEventType::RightReleased) {
    const bool captured=pointer_owned_||layout.panel.contains(event.position); const auto target=pressed_; const bool activate=event.type==InputEventType::LeftReleased&&pointer_owned_&&!dragging_&&target!=PressTarget::None&&target==hit(event.position,layout);
    reset_gesture(); command.captured=captured;
    if(!activate) return command;
    if(target==PressTarget::Close) { close(); command.kind=EconomyCommandKind::Close; }
    else if(target==PressTarget::Refresh) command.kind=EconomyCommandKind::Refresh;
    else if(target>=PressTarget::Priority0&&target<=PressTarget::Priority2&&view.state==EconomyState::Ready&&priority_for(target)!=view.industry_priority) { command.kind=EconomyCommandKind::SetIndustryPriority;command.priority=priority_for(target); }
    return command;
  }
  return command;
}

void NativeEconomyWorkspace::render(DrawList& out,const NativeEconomyView& view,int width,int height) const {
  if(!visible_) return; const auto layout=EconomyLayout::for_viewport(width,height); const auto& rows=cache_for(view,layout,width,height); const auto maximum=std::max(0.f,rows.content_height-layout.body.height); scroll_=std::clamp(scroll_,0.f,maximum);
  native_ui_style::menu_panel(out,layout.panel); fill(out,layout.body,inset);
  text(out,layout.header,layout.panel,"SOVEREIGN TREASURY",gold,layout.heading_font_pixels);
  native_ui_style::panel(out,layout.refresh,false,false); text(out,layout.refresh,layout.refresh,view.state==EconomyState::Ready?"REFRESH":"RETRY",accent,layout.small_font_pixels,TextAlign::Center);
  native_ui_style::panel(out,layout.close,false,false); text(out,layout.close,layout.close,"X",ink,layout.body_font_pixels,TextAlign::Center);
  const auto guidance=view.state==EconomyState::Ready?view.priority_guidance:"Treasury information is unavailable. Retry when the campaign is ready.";
  const auto notice=notice_.empty()?guidance:notice_+"  |  "+guidance;
  text(out,layout.notice,layout.notice,notice,view.state==EconomyState::Ready?muted:warning,layout.small_font_pixels);
  constexpr std::array<const char*,3> labels{"BALANCED","INFRASTRUCTURE","SHIPBUILDING"};
  for(int i=0;i<3;++i) { const bool active=static_cast<int>(view.industry_priority)==i; const bool enabled=view.state==EconomyState::Ready&&!active; native_ui_style::panel(out,layout.priority_buttons[i],false,active); text(out,layout.priority_buttons[i],layout.panel,labels[i],enabled?ink:muted,layout.small_font_pixels,TextAlign::Center); }
  for(const auto& r:rows.rows) {
    UiRect box{layout.body.x,layout.body.y+r.y-scroll_,layout.body.width,r.height};
    if(r.tile) { const auto gap=6.f*layout.scale; const auto tile_width=(layout.body.width-2.f*gap)/3.f; box.x+=static_cast<float>(r.tile_column)*(tile_width+gap);box.width=tile_width; }
    if(const auto visible=intersect(box,layout.body)) {
      fill(out,*visible,row); const auto color=r.warning?warning:r.income?income:muted;
      const UiRect label{box.x+8*layout.scale,box.y+6*layout.scale,
          (r.tile?box.width:box.width*.36f)-16*layout.scale,r.tile?16.f*layout.scale:box.height-12*layout.scale};
      const UiRect value{box.x+(r.tile?8*layout.scale:box.width*.40f+8*layout.scale),
          box.y+(r.tile?23.f:7.f)*layout.scale,(r.tile?box.width:box.width*.60f)-16*layout.scale,
          box.height-(r.tile?28.f:12.f)*layout.scale};
      text(out,label,layout.body,r.left,muted,layout.small_font_pixels);
      text(out,value,layout.body,r.right,color,layout.body_font_pixels,TextAlign::Right);
    }
  }
  if(maximum>0) { const float thumb=std::max(20.f*layout.scale,layout.body.height*layout.body.height/rows.content_height); const float y=layout.body.y+(layout.body.height-thumb)*scroll_/maximum; fill(out,{layout.body.x+layout.body.width-3*layout.scale,layout.body.y,2*layout.scale,layout.body.height},muted); fill(out,{layout.body.x+layout.body.width-3*layout.scale,y,2*layout.scale,thumb},accent); }
}
} // namespace stellar::native_economy
