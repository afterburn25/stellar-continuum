#pragma once
#include "native_menu_style.hpp"
#include <optional>
#include <vector>

namespace stellar::native_ui {
using namespace stellar::native_map;

// One modal choice list shared by settings and campaign setup. Opening a list
// never changes a value; selection is committed only by choosing an item.
class Dropdown final {
 public:
  struct Layout {
    UiRect panel, up, down;
    std::vector<UiRect> rows;
  };
  [[nodiscard]] bool visible() const noexcept { return id_ >= 0; }
  [[nodiscard]] int id() const noexcept { return id_; }
  void close() noexcept { id_=-1; }
  void open(int id,std::vector<std::string> options,int selected) {
    id_=options.empty()?-1:id; options_=std::move(options);
    selected_=std::clamp(selected,0,std::max(0,static_cast<int>(options_.size())-1));
    highlighted_=selected_; first_=std::max(0,selected_-3);
    first_=std::min(first_,std::max(0,static_cast<int>(options_.size())-8));
  }
  [[nodiscard]] Layout layout(UiRect anchor,int width,int height) const {
    Layout result;
    const float row_h=std::clamp(anchor.height,24.f,64.f);
    const int capacity=std::max(1,std::min(8,static_cast<int>((height-56.f)/row_h)));
    const int count=std::min(capacity,static_cast<int>(options_.size()));
    const bool scroll=static_cast<int>(options_.size())>count;
    const float scroll_h=scroll?20.f:0.f;
    const float h=row_h*count+scroll_h*2.f+4.f;
    const float w=std::min(anchor.width,std::max(1.f,width-16.f));
    const float x=std::clamp(anchor.x,8.f,std::max(8.f,width-w-8.f));
    float y=anchor.y+anchor.height+3.f;
    if(y+h>height-8.f)y=anchor.y-h-3.f;
    y=std::clamp(y,8.f,std::max(8.f,height-h-8.f));
    result.panel={x,y,w,h};
    if(scroll){result.up={x+2,y+2,w-4,scroll_h};result.down={x+2,y+h-scroll_h-2,w-4,scroll_h};}
    for(int i=0;i<count;++i)result.rows.push_back({x+2,y+2+scroll_h+i*row_h,w-4,row_h});
    return result;
  }
  [[nodiscard]] std::size_t hover_target(Point point,UiRect anchor,int width,int height) const {
    const auto l=layout(anchor,width,height);
    for(std::size_t i=0;i<l.rows.size();++i)if(l.rows[i].contains(point))return 100+first_+i;
    return l.up.contains(point)?1:l.down.contains(point)?2:0;
  }
  std::optional<int> handle(const InputEvent& event,UiRect anchor,int width,int height) {
    if(!visible())return {};
    const auto l=layout(anchor,width,height);
    const int last=std::max(0,static_cast<int>(options_.size())-static_cast<int>(l.rows.size()));
    first_=std::clamp(first_,0,last);
    if(event.type==InputEventType::EscapePressed||event.type==InputEventType::PointerCancelled){close();return {};}
    if(event.type==InputEventType::Wheel){first_=std::clamp(first_-static_cast<int>(std::round(event.wheel_y)),0,last);return {};}
    if(event.type==InputEventType::PointerMove){
      pointer_=event.position;
      for(int i=0;i<static_cast<int>(l.rows.size());++i)if(l.rows[i].contains(pointer_))highlighted_=first_+i;
    }
    if(event.type==InputEventType::KeyPressed){
      // SDL keycodes are passed through by the platform; arrows / Home / End.
      if(event.key==0x40000052u)highlighted_=std::max(0,highlighted_-1);
      else if(event.key==0x40000051u)highlighted_=std::min(static_cast<int>(options_.size())-1,highlighted_+1);
      else if(event.key==0x4000004au)highlighted_=0;
      else if(event.key==0x4000004du)highlighted_=static_cast<int>(options_.size())-1;
      else if(event.key==13u||event.key==32u){const int value=highlighted_;close();return value;}
      first_=std::clamp(first_,std::max(0,highlighted_-static_cast<int>(l.rows.size())+1),std::min(last,highlighted_));
    }
    if(event.type==InputEventType::LeftPressed){
      if(l.up.height>0&&l.up.contains(event.position)){first_=std::max(0,first_-1);return {};}
      if(l.down.height>0&&l.down.contains(event.position)){first_=std::min(last,first_+1);return {};}
      for(int i=0;i<static_cast<int>(l.rows.size());++i)if(l.rows[i].contains(event.position)){
        const int value=first_+i;close();return value;
      }
      close(); // Consume outside click; never activate an obscured control.
    }
    return {};
  }
  void render(DrawList& out,UiRect anchor,int width,int height,int font) const {
    if(!visible())return;
    const auto l=layout(anchor,width,height);
    out.overlay.emplace_back(FilledRectangle{{l.panel.x+3,l.panel.y+4,l.panel.width,l.panel.height},{0,0,0,165}});
    out.overlay.emplace_back(FilledRectangle{l.panel,{9,27,43,255}});
    out.overlay.emplace_back(StrokedRectangle{l.panel,{111,190,216,255}});
    for(int i=0;i<static_cast<int>(l.rows.size());++i){
      const int index=first_+i;if(index>=static_cast<int>(options_.size()))break;
      const auto r=l.rows[i];
      if(index==highlighted_||index==selected_)out.overlay.emplace_back(FilledRectangle{r,index==highlighted_?Color{35,89,122,255}:Color{23,56,78,255}});
      if(index==selected_)native_menu_style::text(out,{r.x+5,r.y+(r.height-font)*.5f,18.f,r.height},"✓",font,native_menu_style::cyan);
      native_menu_style::text(out,{r.x+26,r.y+(r.height-font)*.5f,r.width-32,r.height},options_[index],font);
    }
    if(l.up.height>0){
      native_menu_style::text(out,l.up,"▲",std::min(font,14),first_>0?native_menu_style::cyan:native_menu_style::muted,TextAlign::Center);
      native_menu_style::text(out,l.down,"▼",std::min(font,14),native_menu_style::cyan,TextAlign::Center);
    }
  }
 private:
  int id_{-1},selected_{},highlighted_{},first_{};
  Point pointer_{};
  std::vector<std::string> options_;
};
}
