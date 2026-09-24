#include "native_accessibility_bridge.hpp"

#ifdef _WIN32
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
#include <atomic>
#include <string>

namespace {

constexpr wchar_t bridge_property[] = L"StellarAccessibilityBridge";

// UTF-8 -> UTF-16 without throwing: invalid input yields an empty string so a
// malformed label can never take the window procedure down.
[[nodiscard]] std::wstring wide(std::string_view value) noexcept {
  if (value.empty()) return {};
  if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                          result.data(), size) != size)
    return {};
  return result;
}

// Minimal server-side provider returned for UiaRootObjectId. It is a
// non-visual pane whose only purpose is sourcing NotificationKind events;
// callers get it through WM_GETOBJECT so the window owns its lifetime.
class AnnouncementProvider final : public IRawElementProviderSimple {
 public:
  explicit AnnouncementProvider(HWND hwnd) noexcept : hwnd_(hwnd) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) noexcept override {
    if (!out) return E_POINTER;
    if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
      *out = static_cast<IRawElementProviderSimple *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const ULONG remaining = --refs_;
    if (remaining == 0) delete this;
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *out) noexcept override {
    if (!out) return E_POINTER;
    *out = ProviderOptions_ServerSideProvider;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown **out) noexcept override {
    if (!out) return E_POINTER;
    *out = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *out) noexcept override {
    if (!out) return E_POINTER;
    VariantInit(out);
    if (id == UIA_NamePropertyId) {
      out->vt = VT_BSTR;
      out->bstrVal = SysAllocString(L"Stellar Continuum");
    } else if (id == UIA_ControlTypePropertyId) {
      out->vt = VT_I4;
      out->lVal = UIA_PaneControlTypeId;
    } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
      out->vt = VT_BOOL;
      out->boolVal = VARIANT_FALSE;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
      IRawElementProviderSimple **out) noexcept override {
    if (!out) return E_POINTER;
    return UiaHostProviderFromHwnd(hwnd_, out);
  }

 private:
  ~AnnouncementProvider() = default;
  HWND hwnd_;
  std::atomic<ULONG> refs_{1};
};

LRESULT CALLBACK bridge_window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
  auto *bridge = static_cast<stellar::native_client::NativeAccessibilityBridge *>(
      GetPropW(hwnd, bridge_property));
  if (bridge)
    return bridge->handle_window_message(reinterpret_cast<std::uintptr_t>(hwnd),
                                         message, wparam, lparam);
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

namespace stellar::native_client {

NativeAccessibilityBridge::~NativeAccessibilityBridge() { detach(); }

bool NativeAccessibilityBridge::attach(void *native_window) {
  detach();
  auto *hwnd = static_cast<HWND>(native_window);
  if (!hwnd || !IsWindow(hwnd)) return false;
  auto *provider = new AnnouncementProvider(hwnd);
  SetPropW(hwnd, bridge_property, static_cast<HANDLE>(this));
  hwnd_ = hwnd;
  provider_ = provider;
  original_proc_ = reinterpret_cast<void *>(SetWindowLongPtrW(
      hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&bridge_window_proc)));
  if (!original_proc_) {
    provider_ = nullptr;
    hwnd_ = nullptr;
    RemovePropW(hwnd, bridge_property);
    provider->Release();
    return false;
  }
  return true;
}

void NativeAccessibilityBridge::detach() {
  auto *hwnd = static_cast<HWND>(hwnd_);
  if (hwnd && IsWindow(hwnd)) {
    if (reinterpret_cast<void *>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) ==
        reinterpret_cast<void *>(&bridge_window_proc))
      SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(original_proc_));
    RemovePropW(hwnd, bridge_property);
  }
  if (provider_) static_cast<AnnouncementProvider *>(provider_)->Release();
  hwnd_ = nullptr;
  provider_ = nullptr;
  original_proc_ = nullptr;
}

bool NativeAccessibilityBridge::announce(std::string_view text) {
  auto *provider = static_cast<IRawElementProviderSimple *>(provider_);
  if (!provider || text.empty() || !UiaClientsAreListening()) return false;
  const std::wstring message = wide(text);
  if (message.empty()) return false;
  BSTR display = SysAllocStringLen(message.data(),
                                   static_cast<UINT>(message.size()));
  if (!display) return false;
  BSTR activity = SysAllocString(L"interface-announcement");
  const HRESULT result =
      UiaRaiseNotificationEvent(provider, NotificationKind_Other,
                                NotificationProcessing_ImportantMostRecent, display,
                                activity);
  SysFreeString(display);
  SysFreeString(activity);
  return SUCCEEDED(result);
}

std::intptr_t NativeAccessibilityBridge::handle_window_message(
    std::uintptr_t window, unsigned message, std::uintptr_t wparam,
    std::intptr_t lparam) {
  auto *hwnd = reinterpret_cast<HWND>(window);
  if (message == WM_GETOBJECT && provider_ &&
      static_cast<DWORD>(lparam) == UiaRootObjectId)
    return UiaReturnRawElementProvider(
        hwnd, wparam, lparam, static_cast<IRawElementProviderSimple *>(provider_));
  return CallWindowProcW(reinterpret_cast<WNDPROC>(original_proc_), hwnd, message,
                         wparam, lparam);
}

} // namespace stellar::native_client

#else

namespace stellar::native_client {

NativeAccessibilityBridge::~NativeAccessibilityBridge() = default;
bool NativeAccessibilityBridge::attach(void *) { return false; }
void NativeAccessibilityBridge::detach() {}
bool NativeAccessibilityBridge::announce(std::string_view) { return false; }
std::intptr_t NativeAccessibilityBridge::handle_window_message(std::uintptr_t,
                                                               unsigned,
                                                               std::uintptr_t,
                                                               std::intptr_t) {
  return 0;
}

} // namespace stellar::native_client

#endif
