#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <stellar/engine/accessibility.hpp>

namespace stellar::native_client {

// Platform assistive-technology bridge: raises OS notification events for each
// drained AccessibilityAnnouncer item so screen readers can announce them.
// Windows supplies a UI Automation provider over the game HWND (WM_GETOBJECT
// subclass) and raises NotificationKind events; other platforms no-op.
// Owner-thread only, matching the surfaces that feed it.
class NativeAccessibilityBridge final {
 public:
  NativeAccessibilityBridge() = default;
  ~NativeAccessibilityBridge();
  NativeAccessibilityBridge(const NativeAccessibilityBridge &) = delete;
  NativeAccessibilityBridge &operator=(const NativeAccessibilityBridge &) = delete;
  NativeAccessibilityBridge(NativeAccessibilityBridge &&) = delete;
  NativeAccessibilityBridge &operator=(NativeAccessibilityBridge &&) = delete;

  // native_window is Window::native_window_handle() (HWND on Windows).
  // Returns false when the platform supplies no backend.
  [[nodiscard]] bool attach(void *native_window);
  void detach();
  [[nodiscard]] bool attached() const noexcept { return hwnd_ != nullptr; }
  // Raises a notification for a live-region announcement. Returns false when
  // unattached, the text is empty, or no assistive client is listening.
  bool announce(std::string_view text);
  // Raises a UIA focus-changed event carrying the label on a synthetic
  // fragment so assistive clients see real focus tracking. Bounds (client
  // pixels) project the control's real rect onto the fragment; control maps
  // to the UIA control type so AT announces "button"/"slider" rather than a
  // generic custom control. Same gates.
  bool focus_changed(
      std::string_view label,
      std::optional<stellar::engine::AnnouncementBounds> bounds = std::nullopt,
      std::optional<stellar::engine::AnnouncementRange> range = std::nullopt,
      stellar::engine::AnnouncementControl control =
          stellar::engine::AnnouncementControl::Custom);
  // Subclassed window-procedure sink installed while attached — platform
  // plumbing for the WM_GETOBJECT answer, not a general event API.
  std::intptr_t handle_window_message(std::uintptr_t hwnd, unsigned message,
                                      std::uintptr_t wparam, std::intptr_t lparam);

 private:
  void *hwnd_{};
  void *provider_{};        // IRawElementProviderSimple*, owned by the bridge
  void *original_proc_{};   // previous WNDPROC
};

} // namespace stellar::native_client
