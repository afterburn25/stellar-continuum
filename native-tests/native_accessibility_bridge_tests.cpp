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
  // The synthetic fragment resolves client-side with the host HWND runtime
  // id ({42, hwnd}) — distinguishing it from the host's spliced native
  // children regardless of the control type the focus announcement set.
  const auto find_fragment = [&](IUIAutomationElement* root)
      -> IUIAutomationElement* {
    IUIAutomationElement* first{};
    if (FAILED(walker->GetFirstChildElement(root, &first))) return nullptr;
    IUIAutomationElement* found{};
    for (IUIAutomationElement* cursor = first; cursor;) {
      SAFEARRAY* rid{};
      (void)cursor->GetRuntimeId(&rid);
      bool match = false;
      if (rid) {
        long lb = 0, ub = -1;
        SafeArrayGetLBound(rid, 1, &lb);
        SafeArrayGetUBound(rid, 1, &ub);
        int* data{};
        if (SUCCEEDED(SafeArrayAccessData(
                rid, reinterpret_cast<void**>(&data)))) {
          // UIA substitutes the host HWND runtime id ({42, hwnd}) for our
          // fragment client-side; spliced native children carry longer rids.
          match = ub - lb + 1 == 2 &&
                  data[1] ==
                      static_cast<int>(
                          reinterpret_cast<std::intptr_t>(host.hwnd));
          SafeArrayUnaccessData(rid);
        }
        SafeArrayDestroy(rid);
      }
      if (match) {
        found = cursor;
        break;
      }
      IUIAutomationElement* next{};
      (void)walker->GetNextSiblingElement(cursor, &next);
      cursor->Release();
      cursor = next;
    }
    if (first && first != found) first->Release();
    return found;
  };
  IUIAutomationElement* child = find_fragment(element);
  require(child != nullptr,
          "focus fragment was not exposed among the root's children");
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
  // The range-bearing announcement resolves to a real Slider control type —
  // a valid range implies the slider role even unclassified.
  CONTROLTYPEID child_type{};
  require(SUCCEEDED(child->get_CurrentControlType(&child_type)) &&
              child_type == UIA_SliderControlTypeId,
          "focus fragment did not report the slider control type");
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
  BOOL read_only = TRUE;
  require(SUCCEEDED(range_pattern->get_CurrentValue(&range_value)) &&
              std::abs(range_value - .64) < 1e-9 &&
              SUCCEEDED(range_pattern->get_CurrentIsReadOnly(&read_only)) &&
              read_only == FALSE,
          "range pattern did not report the slider's value/writable flag");
  // The writable slice: SetValue queues for the owner — latest wins — and
  // drains once for routing to the surface owning the focused slider.
  require(!bridge.take_range_set().has_value(),
          "range-set queue was not empty before SetValue");
  require(SUCCEEDED(range_pattern->SetValue(.3)),
          "UIA SetValue call failed on the writable range");
  const auto queued_set = bridge.take_range_set();
  require(queued_set.has_value() && std::abs(*queued_set - .3) < 1e-9,
          "UIA SetValue did not queue the requested value for the owner");
  require(!bridge.take_range_set().has_value(),
          "drained range-set value was not cleared");
  range_pattern->Release();
  // Sliders stay non-invocable — adjustment is read-only range data.
  IUIAutomationInvokePattern* slider_invoke{};
  require(SUCCEEDED(child->GetCurrentPattern(
              UIA_InvokePatternId,
              reinterpret_cast<IUnknown**>(&slider_invoke))) &&
              slider_invoke == nullptr,
          "slider fragment exposed the invoke pattern");
  child->Release();
  // An explicit control kind maps to the matching UIA control type, and
  // dropping the range retires the pattern rather than leaving it stale.
  (void)bridge.focus_changed(
      "Save", stellar::engine::AnnouncementBounds{8.f, 9.f, 40.f, 16.f},
      std::nullopt, stellar::engine::AnnouncementControl::Button);
  IUIAutomationElement* button_fragment = find_fragment(element);
  require(button_fragment != nullptr,
          "focus fragment was not exposed for the button announcement");
  require(SUCCEEDED(button_fragment->get_CurrentControlType(&child_type)) &&
              child_type == UIA_ButtonControlTypeId,
          "focus fragment did not report the button control type");
  IUIAutomationRangeValuePattern* stale_range{};
  require(SUCCEEDED(button_fragment->GetCurrentPattern(
              UIA_RangeValuePatternId,
              reinterpret_cast<IUnknown**>(&stale_range))) &&
              stale_range == nullptr,
          "focus fragment kept the range pattern on a non-slider control");
  child = button_fragment;
  // The interactive slice: an activatable control exposes the Invoke
  // pattern, and Invoke() queues a pending activation the owner drains
  // into normal input dispatch (Return press+release).
  IUIAutomationInvokePattern* invoke{};
  require(SUCCEEDED(child->GetCurrentPattern(
              UIA_InvokePatternId,
              reinterpret_cast<IUnknown**>(&invoke))) && invoke,
          "focus fragment did not expose the invoke pattern on a button");
  require(bridge.drain_activations() == 0, "activation queue was not empty");
  require(SUCCEEDED(invoke->Invoke()), "UIA Invoke call failed");
  require(SUCCEEDED(invoke->Invoke()), "second UIA Invoke call failed");
  require(bridge.drain_activations() == 2,
          "UIA Invoke calls did not queue activations for the owner");
  require(bridge.drain_activations() == 0,
          "drained activations were not cleared");
  invoke->Release();
  IUIAutomationElement* parent{};
  require(SUCCEEDED(walker->GetParentElement(child, &parent)) && parent,
          "focus fragment did not navigate to its root parent");
  parent->Release();
  // CheckBox focus exposes the toggle pattern reporting the announced
  // checked state; Toggle() queues an activation just like Invoke().
  (void)bridge.focus_changed(
      "Subtitles", stellar::engine::AnnouncementBounds{8.f, 9.f, 40.f, 16.f},
      std::nullopt, stellar::engine::AnnouncementControl::CheckBox, true);
  IUIAutomationElement* toggle_fragment = find_fragment(element);
  require(toggle_fragment != nullptr,
          "focus fragment was not exposed for the checkbox announcement");
  IUIAutomationTogglePattern* toggle{};
  require(SUCCEEDED(toggle_fragment->GetCurrentPattern(
              UIA_TogglePatternId,
              reinterpret_cast<IUnknown**>(&toggle))) && toggle,
          "checkbox fragment did not expose the toggle pattern");
  ToggleState toggle_state{};
  require(SUCCEEDED(toggle->get_CurrentToggleState(&toggle_state)) &&
              toggle_state == ToggleState_On,
          "toggle pattern did not report the announced checked state");
  require(SUCCEEDED(toggle->Toggle()), "UIA Toggle call failed");
  require(bridge.drain_activations() == 1,
          "UIA Toggle did not queue an activation for the owner");
  toggle->Release();
  // Without an announced state the checkbox reports Indeterminate.
  (void)bridge.focus_changed(
      "Unlabelled", std::nullopt, std::nullopt,
      stellar::engine::AnnouncementControl::CheckBox);
  IUIAutomationElement* unknown_toggle = find_fragment(element);
  require(unknown_toggle != nullptr,
          "focus fragment was not exposed for the stateless checkbox");
  IUIAutomationTogglePattern* toggle2{};
  require(SUCCEEDED(unknown_toggle->GetCurrentPattern(
              UIA_TogglePatternId,
              reinterpret_cast<IUnknown**>(&toggle2))) && toggle2,
          "stateless checkbox lost the toggle pattern");
  require(SUCCEEDED(toggle2->get_CurrentToggleState(&toggle_state)) &&
              toggle_state == ToggleState_Indeterminate,
          "stateless checkbox did not report Indeterminate");
  toggle2->Release();
  unknown_toggle->Release();
  toggle_fragment->Release();
  // An Edit focus with a value exposes the value pattern: the text reads
  // back, writable edits queue SetValue for the owner, read-only edits
  // report IsReadOnly and reject writes.
  (void)bridge.focus_changed(
      "Search", stellar::engine::AnnouncementBounds{8.f, 9.f, 40.f, 16.f},
      std::nullopt, stellar::engine::AnnouncementControl::Edit, std::nullopt,
      stellar::engine::AnnouncementValue{"kestrel", true});
  IUIAutomationElement* edit_fragment = find_fragment(element);
  require(edit_fragment != nullptr,
          "focus fragment was not exposed for the edit announcement");
  IUIAutomationValuePattern* value_pattern{};
  require(SUCCEEDED(edit_fragment->GetCurrentPattern(
              UIA_ValuePatternId,
              reinterpret_cast<IUnknown**>(&value_pattern))) && value_pattern,
          "edit fragment did not expose the value pattern");
  BSTR edit_text{};
  require(SUCCEEDED(value_pattern->get_CurrentValue(&edit_text)) && edit_text &&
              std::wstring(edit_text) == L"kestrel",
          "value pattern did not report the announced edit text");
  SysFreeString(edit_text);
  require(SUCCEEDED(value_pattern->get_CurrentIsReadOnly(&read_only)) &&
              read_only == FALSE,
          "writable edit reported itself read-only");
  require(!bridge.take_text_set().has_value(),
          "text-set queue was not empty before SetValue");
  BSTR replacement = SysAllocString(L"voideyes");
  require(replacement != nullptr, "SysAllocString failed");
  require(SUCCEEDED(value_pattern->SetValue(replacement)),
          "UIA SetValue failed on a writable edit");
  const auto queued_text = bridge.take_text_set();
  require(queued_text.has_value() && *queued_text == "voideyes",
          "UIA SetValue did not queue the replacement text");
  require(!bridge.take_text_set().has_value(),
          "drained text-set value was not cleared");
  value_pattern->Release();
  edit_fragment->Release();
  // A read-only edit (the setup seed field) reports the text but refuses
  // SetValue — nothing queues for the owner.
  (void)bridge.focus_changed(
      "Seed", std::nullopt, std::nullopt,
      stellar::engine::AnnouncementControl::Edit, std::nullopt,
      stellar::engine::AnnouncementValue{"42", false});
  IUIAutomationElement* seed_fragment = find_fragment(element);
  require(seed_fragment != nullptr,
          "focus fragment was not exposed for the read-only edit");
  IUIAutomationValuePattern* seed_value{};
  require(SUCCEEDED(seed_fragment->GetCurrentPattern(
              UIA_ValuePatternId,
              reinterpret_cast<IUnknown**>(&seed_value))) && seed_value,
          "read-only edit lost the value pattern");
  require(SUCCEEDED(seed_value->get_CurrentIsReadOnly(&read_only)) &&
              read_only == TRUE,
          "read-only edit did not report IsReadOnly");
  require(FAILED(seed_value->SetValue(replacement)),
          "UIA SetValue succeeded on a read-only edit");
  SysFreeString(replacement);
  require(!bridge.take_text_set().has_value(),
          "read-only SetValue queued text for the owner");
  seed_value->Release();
  seed_fragment->Release();
  // An empty focus label is the ring-release signal — the fragment must
  // stop claiming keyboard focus so AT stops tracking a stale control.
  (void)bridge.focus_changed("");
  // UIA may cache properties per resolved element — re-walk for a fresh
  // fragment instance and check the live flag there.
  IUIAutomationElement* released_fragment = find_fragment(element);
  require(released_fragment != nullptr,
          "focus fragment disappeared after release");
  require(SUCCEEDED(
              released_fragment->get_CurrentHasKeyboardFocus(&has_focus)) &&
              has_focus == FALSE,
          "focus fragment still claimed keyboard focus after release");
  require(SUCCEEDED(released_fragment->get_CurrentControlType(&child_type)) &&
              child_type == UIA_CustomControlTypeId,
          "focus fragment kept the button control type after release");
  IUIAutomationInvokePattern* released_invoke{};
  require(SUCCEEDED(released_fragment->GetCurrentPattern(
              UIA_InvokePatternId,
              reinterpret_cast<IUnknown**>(&released_invoke))) &&
              released_invoke == nullptr,
          "released fragment kept the invoke pattern");
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
