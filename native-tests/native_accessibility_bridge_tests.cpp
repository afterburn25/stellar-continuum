#include "native_accessibility_bridge.hpp"

#include <cmath>
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
  // focus_changed() projects a synthetic fragment: the root's tree-walk
  // child reports the label, and GetFocus resolves it.
  (void)bridge.focus_changed("Fleet Atlas",
                             stellar::engine::AnnouncementBounds{5.f, 6.f, 30.f, 20.f},
                             stellar::engine::AnnouncementRange{0., 1., .64});
  IUIAutomationTreeWalker* walker{};
  require(SUCCEEDED(automation->get_RawViewWalker(&walker)) && walker,
          "UIA raw view walker was not available");
  // UIA splices the host HWND's native children (title bar, etc.) into a
  // hostable provider — walk the raw children until the focus fragment shows
  // up with its appended runtime id.
  IUIAutomationElement* child{};
  require(SUCCEEDED(walker->GetFirstChildElement(element, &child)),
          "raw child walk failed");
  IUIAutomationElement* fragment{};
  for (IUIAutomationElement* cursor = child; cursor;) {
    CONTROLTYPEID ct{};
    (void)cursor->get_CurrentControlType(&ct);
    SAFEARRAY* rid{};
    (void)cursor->GetRuntimeId(&rid);
    long length = 0;
    if (rid) {
      long lb = 0, ub = -1;
      SafeArrayGetLBound(rid, 1, &lb);
      SafeArrayGetUBound(rid, 1, &ub);
      length = ub - lb + 1;
      SafeArrayDestroy(rid);
    }
    if (ct == UIA_CustomControlTypeId && length == 2) {
      fragment = cursor;
      break;
    }
    IUIAutomationElement* next{};
    (void)walker->GetNextSiblingElement(cursor, &next);
    cursor->Release();
    cursor = next;
  }
  if (child && child != fragment) child->Release();
  require(fragment != nullptr,
          "focus fragment was not exposed among the root's children");
  child = fragment;
  BSTR child_name{};
  const HRESULT name_hr = child->get_CurrentName(&child_name);
  if (!(SUCCEEDED(name_hr) && child_name &&
        std::wstring(child_name) == L"Fleet Atlas")) {
    std::fprintf(stderr, "child name hr=%lx value='%ls'\n",
                 static_cast<unsigned long>(name_hr),
                 child_name ? child_name : L"<null>");
    throw std::runtime_error("focus fragment did not carry the focused label");
  }
  SysFreeString(child_name);
  CONTROLTYPEID child_type{};
  require(SUCCEEDED(child->get_CurrentControlType(&child_type)) &&
              child_type == UIA_CustomControlTypeId,
          "focus fragment reported the wrong control type");
  BOOL has_focus = FALSE;
  require(SUCCEEDED(child->get_CurrentHasKeyboardFocus(&has_focus)) &&
              has_focus == TRUE,
          "focus fragment did not report keyboard focus");
  // The passed client-space bounds must surface as the fragment's
  // screen-space bounding rectangle.
  RECT bounds{};
  require(SUCCEEDED(child->get_CurrentBoundingRectangle(&bounds)),
          "focus fragment did not report a bounding rectangle");
  POINT corner{5, 6};
  require(ClientToScreen(host.hwnd, &corner) == TRUE,
          "ClientToScreen failed on the host window");
  require(bounds.left == corner.x && bounds.top == corner.y &&
              bounds.right - bounds.left == 30 &&
              bounds.bottom - bounds.top == 20,
          "focus fragment did not project the focused control's bounds");
  // A slider focus announcement exposes the range pattern with the live
  // value — AT reports position, not just label text.
  IUIAutomationRangeValuePattern* range_pattern{};
  const HRESULT pattern_hr = child->GetCurrentPattern(
      UIA_RangeValuePatternId,
      reinterpret_cast<IUnknown**>(&range_pattern));
  if (!(SUCCEEDED(pattern_hr) && range_pattern))
    std::fprintf(stderr, "range pattern hr=%lx pattern=%p\n",
                 static_cast<unsigned long>(pattern_hr),
                 static_cast<void*>(range_pattern));
  require(SUCCEEDED(pattern_hr) && range_pattern,
          "focus fragment did not expose the range pattern for a slider");
  double range_value{};
  BOOL read_only = FALSE;
  require(SUCCEEDED(range_pattern->get_CurrentValue(&range_value)) &&
              std::abs(range_value - .64) < 1e-9 &&
              SUCCEEDED(range_pattern->get_CurrentIsReadOnly(&read_only)) &&
              read_only == TRUE,
          "range pattern did not report the slider's value/read-only flag");
  range_pattern->Release();
  IUIAutomationElement* parent{};
  require(SUCCEEDED(walker->GetParentElement(child, &parent)) && parent,
          "focus fragment did not navigate to its root parent");
  parent->Release();
  // An empty focus label is the ring-release signal — the fragment must
  // stop claiming keyboard focus so AT stops tracking a stale control.
  (void)bridge.focus_changed("");
  // UIA may cache properties per resolved element — re-walk for a fresh
  // fragment instance and check the live flag there.
  IUIAutomationElement* released_child{};
  require(SUCCEEDED(walker->GetFirstChildElement(element, &released_child)),
          "raw child walk failed after release");
  IUIAutomationElement* released_fragment{};
  for (IUIAutomationElement* cursor = released_child; cursor;) {
    CONTROLTYPEID ct{};
    (void)cursor->get_CurrentControlType(&ct);
    SAFEARRAY* rid{};
    (void)cursor->GetRuntimeId(&rid);
    long length = 0;
    if (rid) {
      long lb = 0, ub = -1;
      SafeArrayGetLBound(rid, 1, &lb);
      SafeArrayGetUBound(rid, 1, &ub);
      length = ub - lb + 1;
      SafeArrayDestroy(rid);
    }
    if (ct == UIA_CustomControlTypeId && length == 2) {
      released_fragment = cursor;
      break;
    }
    IUIAutomationElement* next{};
    (void)walker->GetNextSiblingElement(cursor, &next);
    cursor->Release();
    cursor = next;
  }
  if (released_child && released_child != released_fragment)
    released_child->Release();
  require(released_fragment != nullptr,
          "focus fragment disappeared after release");
  require(SUCCEEDED(
              released_fragment->get_CurrentHasKeyboardFocus(&has_focus)) &&
              has_focus == FALSE,
          "focus fragment still claimed keyboard focus after release");
  released_fragment->Release();
  child->Release();
  walker->Release();

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
