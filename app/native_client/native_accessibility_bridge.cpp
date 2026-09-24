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
#include <cmath>
#include <string>

namespace {

constexpr wchar_t bridge_property[] = L"StellarAccessibilityBridge";

// UTF-8 -> UTF-16 without throwing: invalid input yields an empty string so a
// malformed label can never take the window procedure down.
[[nodiscard]] std::wstring wide(std::string_view value) noexcept {
  if (value.empty()) return {};
  if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, value.data(),
                          static_cast<int>(value.size()), result.data(),
                          size) != size)
    return {};
  return result;
}

[[nodiscard]] SAFEARRAY *runtime_id(int id) noexcept {
  SAFEARRAY *array = SafeArrayCreateVector(VT_I4, 0, 2);
  if (!array) return nullptr;
  int *data = nullptr;
  if (FAILED(SafeArrayAccessData(array, reinterpret_cast<void **>(&data)))) {
    SafeArrayDestroy(array);
    return nullptr;
  }
  data[0] = UiaAppendRuntimeId;
  data[1] = id;
  SafeArrayUnaccessData(array);
  return array;
}

class WindowProvider;
class FocusFragment;

// Shared IUnknown + IRawElementProviderSimple plumbing for both providers.
class ProviderBase : public IRawElementProviderSimple {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void **out) noexcept override {
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

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(
      ProviderOptions *out) noexcept override {
    if (!out) return E_POINTER;
    *out = ProviderOptions_ServerSideProvider;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,
                                               IUnknown **out) noexcept override {
    if (!out) return E_POINTER;
    *out = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
      IRawElementProviderSimple **out) noexcept override {
    if (!out) return E_POINTER;
    return UiaHostProviderFromHwnd(hwnd(), out);
  }

 protected:
  virtual HWND hwnd() const noexcept = 0;
  // Release() deletes through this pointer — the destructor must dispatch
  // virtually so WindowProvider can release its fragment child.
  virtual ~ProviderBase() = default;
  std::atomic<ULONG> refs_{1};
};

// Synthetic fragment for the control that currently owns keyboard focus. The
// dispatcher supplies its label per focus move; surfaces do not yet project
// geometry, so the bounding rectangle reports the window itself.
class FocusFragment final : public ProviderBase,
                            public IRawElementProviderFragment {
 public:
  FocusFragment(WindowProvider *root, HWND host) noexcept
      : root_(root), host_(host) {}

  void set_label(std::wstring label, std::optional<RECT> rect) {
    label_ = std::move(label);
    rect_ = rect;
  }

  // IUnknown is implemented once here so both interface bases resolve to the
  // same counter.
  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return ProviderBase::AddRef();
  }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    return ProviderBase::Release();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void **out) noexcept override {
    if (iid == IID_IRawElementProviderFragment) {
      if (!out) return E_POINTER;
      *out = static_cast<IRawElementProviderFragment *>(this);
      AddRef();
      return S_OK;
    }
    return ProviderBase::QueryInterface(iid, out);
  }

  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,
                                             VARIANT *out) noexcept override {
    if (!out) return E_POINTER;
    VariantInit(out);
    if (id == UIA_NamePropertyId && !label_.empty()) {
      out->vt = VT_BSTR;
      out->bstrVal = SysAllocStringLen(label_.data(),
                                       static_cast<UINT>(label_.size()));
    } else if (id == UIA_ControlTypePropertyId) {
      out->vt = VT_I4;
      out->lVal = UIA_CustomControlTypeId;
    } else if (id == UIA_HasKeyboardFocusPropertyId) {
      out->vt = VT_BOOL;
      out->boolVal = VARIANT_TRUE;
    } else if (id == UIA_IsKeyboardFocusablePropertyId) {
      out->vt = VT_BOOL;
      out->boolVal = VARIANT_TRUE;
    } else if (id == UIA_IsControlElementPropertyId ||
               id == UIA_IsContentElementPropertyId) {
      out->vt = VT_BOOL;
      out->boolVal = VARIANT_FALSE;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                     IRawElementProviderFragment **out) noexcept
      override;
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **out) noexcept override {
    if (!out) return E_POINTER;
    *out = runtime_id(1);
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(
      UiaRect *out) noexcept override {
    if (!out) return E_POINTER;
    // Screen coordinates — the stored rect is client-space until projected.
    RECT rect{};
    if (rect_) {
      rect = *rect_;
      POINT corner{rect.left, rect.top};
      ClientToScreen(host_, &corner);
      OffsetRect(&rect, corner.x - rect.left, corner.y - rect.top);
    } else {
      GetWindowRect(host_, &rect);
    }
    out->left = rect.left;
    out->top = rect.top;
    out->width = static_cast<double>(rect.right - rect.left);
    out->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(
      SAFEARRAY **out) noexcept override {
    if (!out) return E_POINTER;
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetFocus() noexcept override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(
      IRawElementProviderFragmentRoot **out) noexcept override;

 protected:
  HWND hwnd() const noexcept override { return host_; }

 private:
  ~FocusFragment() override = default;
  WindowProvider *root_;
  HWND host_;
  std::wstring label_;
  std::optional<RECT> rect_;
};

// Fragment root answered for UiaRootObjectId: sources live-region
// notifications and reports the synthetic focus fragment through GetFocus.
class WindowProvider final : public ProviderBase,
                             public IRawElementProviderFragment,
                             public IRawElementProviderFragmentRoot {
 public:
  explicit WindowProvider(HWND hwnd) : host_(hwnd) {
    focus_ = new FocusFragment(this, hwnd);
  }

  FocusFragment *focus_fragment() noexcept { return focus_; }
  void set_focus_label(std::wstring label, std::optional<RECT> rect) {
    focus_->set_label(std::move(label), std::move(rect));
    focused_ = true;
  }
  [[nodiscard]] bool has_focus() const noexcept { return focused_; }

  // IUnknown is implemented once here so all three interface bases resolve
  // to the same counter.
  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return ProviderBase::AddRef();
  }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    return ProviderBase::Release();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void **out) noexcept override {
    if (iid == IID_IRawElementProviderFragment) {
      if (!out) return E_POINTER;
      *out = static_cast<IRawElementProviderFragment *>(this);
      AddRef();
      return S_OK;
    }
    if (iid == IID_IRawElementProviderFragmentRoot) {
      if (!out) return E_POINTER;
      *out = static_cast<IRawElementProviderFragmentRoot *>(this);
      AddRef();
      return S_OK;
    }
    return ProviderBase::QueryInterface(iid, out);
  }

  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,
                                             VARIANT *out) noexcept override {
    if (!out) return E_POINTER;
    VariantInit(out);
    if (id == UIA_NamePropertyId) {
      out->vt = VT_BSTR;
      out->bstrVal = SysAllocString(L"Stellar Continuum");
    } else if (id == UIA_ControlTypePropertyId) {
      out->vt = VT_I4;
      out->lVal = UIA_PaneControlTypeId;
    } else if (id == UIA_IsControlElementPropertyId ||
               id == UIA_IsContentElementPropertyId) {
      out->vt = VT_BOOL;
      out->boolVal = VARIANT_FALSE;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                     IRawElementProviderFragment **out) noexcept
      override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (direction == NavigateDirection_FirstChild && focus_) {
      *out = static_cast<IRawElementProviderFragment *>(focus_);
      (*out)->AddRef();
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **out) noexcept override {
    if (!out) return E_POINTER;
    *out = SafeArrayCreateVector(VT_I4, 0, 0);
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(
      UiaRect *out) noexcept override {
    if (!out) return E_POINTER;
    RECT rect{};
    GetWindowRect(host_, &rect);
    out->left = rect.left;
    out->top = rect.top;
    out->width = static_cast<double>(rect.right - rect.left);
    out->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(
      SAFEARRAY **out) noexcept override {
    if (!out) return E_POINTER;
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetFocus() noexcept override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(
      IRawElementProviderFragmentRoot **out) noexcept override {
    if (!out) return E_POINTER;
    *out = static_cast<IRawElementProviderFragmentRoot *>(this);
    AddRef();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
      double, double, IRawElementProviderFragment **out) noexcept override {
    if (!out) return E_POINTER;
    *out = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFocus(
      IRawElementProviderFragment **out) noexcept override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (focused_ && focus_) {
      *out = static_cast<IRawElementProviderFragment *>(focus_);
      (*out)->AddRef();
    }
    return S_OK;
  }

 protected:
  HWND hwnd() const noexcept override { return host_; }

 private:
  ~WindowProvider() override {
    if (focus_)
      static_cast<IRawElementProviderSimple *>(focus_)->Release();
  }
  HWND host_;
  FocusFragment *focus_{};
  bool focused_{};
};

HRESULT STDMETHODCALLTYPE FocusFragment::Navigate(
    NavigateDirection direction, IRawElementProviderFragment **out) noexcept {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (direction == NavigateDirection_Parent && root_) {
    *out = static_cast<IRawElementProviderFragment *>(root_);
    (*out)->AddRef();
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE FocusFragment::get_FragmentRoot(
    IRawElementProviderFragmentRoot **out) noexcept {
  if (!out) return E_POINTER;
  if (!root_) {
    *out = nullptr;
    return S_OK;
  }
  return root_->QueryInterface(IID_IRawElementProviderFragmentRoot,
                               reinterpret_cast<void **>(out));
}

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
  auto *provider = new WindowProvider(hwnd);
  SetPropW(hwnd, bridge_property, static_cast<HANDLE>(this));
  hwnd_ = hwnd;
  provider_ = provider;
  original_proc_ = reinterpret_cast<void *>(SetWindowLongPtrW(
      hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&bridge_window_proc)));
  if (!original_proc_) {
    provider_ = nullptr;
    hwnd_ = nullptr;
    RemovePropW(hwnd, bridge_property);
    static_cast<IRawElementProviderSimple *>(provider)->Release();
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
  if (provider_)
    static_cast<IRawElementProviderSimple *>(
        static_cast<WindowProvider *>(provider_))
        ->Release();
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
                                NotificationProcessing_ImportantMostRecent,
                                display, activity);
  SysFreeString(display);
  SysFreeString(activity);
  return SUCCEEDED(result);
}

bool NativeAccessibilityBridge::focus_changed(
    std::string_view label,
    std::optional<stellar::engine::AnnouncementBounds> bounds) {
  auto *provider = static_cast<WindowProvider *>(provider_);
  if (!provider || label.empty()) return false;
  std::wstring name = wide(label);
  if (name.empty()) return false;
  std::optional<RECT> rect;
  if (bounds) {
    const auto finite = [](float v) { return std::isfinite(v); };
    if (finite(bounds->x) && finite(bounds->y) && finite(bounds->width) &&
        finite(bounds->height) && bounds->width > 0.f && bounds->height > 0.f)
      rect = RECT{static_cast<LONG>(std::lround(bounds->x)),
                  static_cast<LONG>(std::lround(bounds->y)),
                  static_cast<LONG>(std::lround(bounds->x + bounds->width)),
                  static_cast<LONG>(std::lround(bounds->y + bounds->height))};
  }
  // The fragment reflects real focus state regardless of listeners; only the
  // event raise is gated on an assistive client being attached.
  provider->set_focus_label(std::move(name), rect);
  if (!UiaClientsAreListening()) return false;
  return SUCCEEDED(UiaRaiseAutomationEvent(
      static_cast<IRawElementProviderSimple *>(provider->focus_fragment()),
      UIA_AutomationFocusChangedEventId));
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
bool NativeAccessibilityBridge::focus_changed(
    std::string_view, std::optional<stellar::engine::AnnouncementBounds>) {
  return false;
}
std::intptr_t NativeAccessibilityBridge::handle_window_message(std::uintptr_t,
                                                               unsigned,
                                                               std::uintptr_t,
                                                               std::intptr_t) {
  return 0;
}

} // namespace stellar::native_client

#endif
