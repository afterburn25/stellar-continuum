#include "native_accessibility_bridge.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
#include <ole2.h>
#include <UIAutomation.h>

using namespace stellar::native_client;

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

struct MessageWindow final {
  MessageWindow() {
    hwnd = CreateWindowExW(0, L"STATIC", L"accessibility-bridge-test",
                           WS_OVERLAPPED, 0, 0, 10, 10, HWND_MESSAGE, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) throw std::runtime_error("message-only test window was not created");
  }
  ~MessageWindow() { if (hwnd) DestroyWindow(hwnd); }
  HWND hwnd{};
};

struct ComScope final {
  ComScope() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
      throw std::runtime_error("COM initialization failed");
  }
  ~ComScope() { CoUninitialize(); }
};
}

int main() try {
  NativeAccessibilityBridge detached;
  require(!detached.attached() && !detached.announce("ignored"),
          "unattached bridge claimed reachability");
  require(!detached.attach(nullptr), "bridge attached to a null window");

  MessageWindow host;
  require(SendMessageW(host.hwnd, WM_GETOBJECT, 0, UiaRootObjectId) == 0,
          "unbridged window answered a UIA root request");

  NativeAccessibilityBridge bridge;
  require(bridge.attach(host.hwnd) && bridge.attached(),
          "bridge did not attach to a message-only window");

  // WM_GETOBJECT must return the UIA-reserved provider handle — the value is
  // opaque to callers, so a real client validates what it resolves to.
  const LRESULT raw =
      SendMessageW(host.hwnd, WM_GETOBJECT, 0, static_cast<LPARAM>(UiaRootObjectId));
  require(raw != 0, "attached window did not answer UiaRootObjectId");

  require(SendMessageW(host.hwnd, WM_GETOBJECT, 0, 0) == 0,
          "non-root WM_GETOBJECT did not fall through to the original proc");

  // End-to-end: the UIA client API resolves our provider for the window.
  ComScope com;
  IUIAutomation* automation{};
  require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                     CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                     reinterpret_cast<void**>(&automation))) &&
              automation,
          "UI Automation client was not created");
  IUIAutomationElement* element{};
  require(SUCCEEDED(automation->ElementFromHandle(host.hwnd, &element)) && element,
          "UIA did not resolve the bridge provider for the window");
  BSTR name{};
  require(SUCCEEDED(element->get_CurrentName(&name)) && name &&
              std::wstring(name) == L"Stellar Continuum",
          "UIA element did not report the application name");
  SysFreeString(name);
  CONTROLTYPEID control_type{};
  require(SUCCEEDED(element->get_CurrentControlType(&control_type)) &&
              control_type == UIA_PaneControlTypeId,
          "UIA element did not report the pane control type");
  BOOL is_control = TRUE;
  require(SUCCEEDED(element->get_CurrentIsControlElement(&is_control)) &&
              is_control == FALSE,
          "notification provider leaked into the control tree");
  element->Release();
  automation->Release();

  // announce() may return false when no assistive client is listening; it must
  // still accept UTF-8 text without disturbing the provider.
  (void)bridge.announce("Fleet moved to Sol");
  require(SendMessageW(host.hwnd, WM_GETOBJECT, 0,
                       static_cast<LPARAM>(UiaRootObjectId)) != 0,
          "announce() disturbed the UIA provider");

  bridge.detach();
  require(!bridge.attached() &&
              SendMessageW(host.hwnd, WM_GETOBJECT, 0,
                           static_cast<LPARAM>(UiaRootObjectId)) == 0,
          "detached window still answered UiaRootObjectId");

  std::cout << "native_accessibility_bridge tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "native_accessibility_bridge test failure: " << error.what() << '\n';
  return 1;
}
