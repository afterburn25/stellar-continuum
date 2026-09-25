#include "native_body_inspection_panel.hpp"
#include <algorithm>
#include <cmath>

namespace stellar::native_system_ui {
using namespace stellar::native_map;
namespace {
constexpr Color ink{222,237,249,255},muted{150,181,202,255},cyan{113,217,232,255};
UiRect intersection(UiRect a,UiRect b){
  const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);
  return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),
              std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};
}
void draw_text(DrawList& out,UiRect box,const std::string& value,Color color,int size,UiRect clip){
  const auto visible=intersection(box,clip);
  if(visible.width>0.f&&visible.height>0.f)
    out.overlay.emplace_back(Text{{box.x,box.y},value,color,size,box.width,visible});
}
}
void BodyInspectionPanel::set_inspection(std::optional<BodyInspection> value){
  if(value==value_)return;
  if(!value||!value_||value->body_id!=value_->body_id)scroll_.scroll_offset=0.f;
  value_=std::move(value);valid_=false;
}
void BodyInspectionPanel::set_text_measurer(std::function<TextExtent(const Text&)> value){measure_=std::move(value);valid_=false;}
void BodyInspectionPanel::clear(){value_.reset();items_.clear();scroll_={};valid_=false;}
void BodyInspectionPanel::layout(UiRect panel,float footer_top) const {
  if(!value_)return;
  if(valid_&&panel.x==panel_.x&&panel.y==panel_.y&&panel.width==panel_.width&&panel.height==panel_.height&&footer_top==footer_top_)return;
  panel_=panel;footer_top_=footer_top;items_.clear();
  const auto height=[&](const std::string& value,float width,int font){
    if(measure_)return static_cast<float>(std::max(1,measure_(Text{{},value,ink,font,width}).height));
    return std::max(1.f,std::ceil(static_cast<float>(value.size())/std::max(1.f,width/(font*.6f))))*(font+5.f);
  };
  const float width=std::max(1.f,panel.width-32.f);
  name_height_=std::min(height(value_->name,width,17),std::max(24.f,footer_top-panel.y-200.f));
  const float body_top=panel.y+50.f+name_height_+25.f;
  body_={panel.x+14.f,body_top,width,std::max(0.f,footer_top-8.f-body_top)};
  float y=0.f;
  for(const auto& section:value_->sections){
    const float heading_height=height(section.heading,width,12);
    items_.push_back({0.f,y,width,heading_height,section.heading,true});y+=heading_height+10.f;
    for(const auto& fact:section.facts){
      const float left_width=width*.44f,right_x=width*.47f,right_width=width*.53f;
      const auto label_height=height(fact.label,left_width,12);
      const auto value_height=height(fact.value,right_width,14);
      items_.push_back({0.f,y,left_width,label_height,fact.label,false});
      items_.push_back({right_x,y,right_width,value_height,fact.value,false});
      y+=std::max(24.f,std::max(label_height,value_height)+7.f);
    }
    y+=12.f;
  }
  scroll_.sync(y,body_.height);valid_=true;
}
void BodyInspectionPanel::scroll(float wheel,UiRect panel,float footer_top){
  layout(panel,footer_top);
  scroll_.scroll_by(-wheel*42.f);
}
void BodyInspectionPanel::render(DrawList& out,UiRect panel,float footer_top) const {
  if(!value_)return;layout(panel,footer_top);
  const auto heading=locale_&&locale_->contains("SYSTEM_INSPECTOR")?std::string(locale_->translate("SYSTEM_INSPECTOR")):std::string("SYSTEM INSPECTOR");
  draw_text(out,{panel.x+14.f,panel.y+14.f,panel.width-28.f,28.f},heading,ink,18,panel);
  draw_text(out,{panel.x+14.f,panel.y+47.f,panel.width-32.f,name_height_},value_->name,ink,17,panel);
  draw_text(out,{panel.x+14.f,panel.y+50.f+name_height_,panel.width-28.f,20.f},value_->survey_status,
            value_->confirmed?Color{109,229,174,255}:Color{248,195,109,255},12,panel);
  for(const auto& item:items_){
    const int font=item.heading?12:item.x==0.f?12:14;
    draw_text(out,{body_.x+item.x,body_.y+item.y-scroll_.scroll_offset,item.width,item.height},item.text,
              item.heading?cyan:item.x==0.f?muted:ink,font,body_);
  }
  if(const auto thumb=scroll_.thumb(body_.height,24.f);thumb.size>0.f){
    out.overlay.emplace_back(FilledRectangle{{panel.x+panel.width-8.f,body_.y,2.f,body_.height},{29,59,75,255}});
    out.overlay.emplace_back(FilledRectangle{{panel.x+panel.width-8.f,body_.y+thumb.offset,2.f,thumb.size},cyan});
  }
}
}
