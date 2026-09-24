#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <utility>

namespace stellar::native_menu_audio {
// Zero means background or a disabled item. Only an actual pointer entry plays
// the existing effect; rendering and movement inside one control remain silent.
inline std::uint64_t hit(stellar::native_map::Point point,
                         std::initializer_list<stellar::native_map::UiRect> controls) {
  std::uint64_t id=0;
  for(const auto& bounds:controls){++id;if(bounds.width>0&&bounds.height>0&&bounds.contains(point))return id;}
  return 0;
}
class HoverFeedback {
public:
  void set_callback(std::function<void()> callback){callback_=std::move(callback);reset();}
  void reset() noexcept { target_=0; }
  void update(const stellar::native_map::InputEvent& event,std::uint64_t target){
    using stellar::native_map::InputEventType;
    if(event.type==InputEventType::PointerCancelled||event.type==InputEventType::EscapePressed||event.type==InputEventType::Wheel){reset();return;}
    if(event.type!=InputEventType::PointerMove)return;
    const auto previous=std::exchange(target_,target);
    if(target&&target!=previous&&callback_)callback_();
  }
  // Keyboard/focus navigation: play the cue for the focused control without
  // replacing the pointer's hover target.
  void cue(std::uint64_t target){if(target&&target!=target_&&callback_)callback_();}
private:
  std::function<void()> callback_;
  std::uint64_t target_{};
};
}
