#pragma once
#include <stellar/engine/developer_access.hpp>
#include <stellar/engine/native_map_platform.hpp>

namespace stellar::native_map {
inline bool is_developer_shortcut(const InputEvent &event) noexcept {
  // SDL scancode-key bit plus F12 (69); the platform preserves modifiers.
  return event.type==InputEventType::KeyPressed&&event.key==0x40000045u&&
      event.control&&event.shift&&!event.alt;
}
}
