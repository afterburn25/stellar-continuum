// Windows SAPI 5 offline speech backend — the native port of
// WindowsSapiSpeechBackend. All COM access stays on a dedicated STA worker;
// output is a 22.05 kHz, 16-bit mono PCM WAV file.
#include "native_voice_playback.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <synchapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#endif

namespace stellar::native_voice {

#ifdef _WIN32
namespace {

struct InstalledVoice {
  std::wstring id, description, gender, culture;
};

template <typename T> struct Com {
  T *p{};
  ~Com() { if (p) p->Release(); }
  T *operator->() const { return p; }
  T **put() { return &p; }
  explicit operator bool() const { return p != nullptr; }
  Com() = default;
  Com(const Com &) = delete;
  Com &operator=(const Com &) = delete;
};

[[nodiscard]] std::wstring wide(std::string_view value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), length);
  return result;
}

[[nodiscard]] std::string narrow(std::wstring_view value) {
  if (value.empty()) return {};
  const int length = WideCharToMultiByte(
      CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
      nullptr, nullptr);
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), length,
                      nullptr, nullptr);
  return result;
}

[[nodiscard]] std::wstring token_attribute(ISpObjectToken *token,
                                           const wchar_t *name) {
  Com<ISpDataKey> attributes;
  if (FAILED(token->OpenKey(L"Attributes", attributes.put())) || !attributes)
    return {};
  LPWSTR value = nullptr;
  if (FAILED(attributes->GetStringValue(name, &value)) || !value) return {};
  std::wstring result = value;
  CoTaskMemFree(value);
  return result;
}

// "Language" is a semicolon list of hex LCIDs; the first wins, as in the
// reference ParseLanguage.
[[nodiscard]] std::wstring parse_language(std::wstring_view value) {
  const auto semicolon = value.find(L';');
  auto first = std::wstring(value.substr(
      0, semicolon == std::wstring_view::npos ? value.size() : semicolon));
  while (!first.empty() && std::iswspace(first.front())) first.erase(0, 1);
  while (!first.empty() && std::iswspace(first.back())) first.pop_back();
  if (first.empty()) return {};
  const auto lcid = std::wcstoul(first.c_str(), nullptr, 16);
  wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
  if (LCIDToLocaleName(static_cast<LCID>(lcid), name,
                       LOCALE_NAME_MAX_LENGTH, 0) > 0)
    return name;
  return first;
}

// sphelper.h pulls in ATL which is not available in the build toolchain; open
// the voices category directly.
[[nodiscard]] HRESULT open_voices_category(ISpObjectTokenCategory **out) {
  *out = nullptr;
  HRESULT status =
      CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                       IID_ISpObjectTokenCategory,
                       reinterpret_cast<void **>(out));
  if (FAILED(status) || !*out) return status;
  status = (*out)->SetId(SPCAT_VOICES, FALSE);
  if (FAILED(status)) {
    (*out)->Release();
    *out = nullptr;
  }
  return status;
}

[[nodiscard]] std::vector<InstalledVoice> discover_voices() {
  std::vector<InstalledVoice> result;
  Com<ISpObjectTokenCategory> category;
  if (FAILED(open_voices_category(category.put()))) return result;
  Com<IEnumSpObjectTokens> tokens;
  if (FAILED(category->EnumTokens(nullptr, nullptr, tokens.put())))
    return result;
  for (;;) {
    ISpObjectToken *raw = nullptr;
    ULONG fetched = 0;
    if (FAILED(tokens->Next(1, &raw, &fetched)) || fetched == 0 || !raw)
      break;
    Com<ISpObjectToken> token;
    token.p = raw;
    LPWSTR id = nullptr;
    if (FAILED(token->GetId(&id)) || !id) continue;
    LPWSTR description = nullptr;
    // The token's default registry value is the display name.
    token->GetStringValue(nullptr, &description);
    InstalledVoice voice;
    voice.id = id;
    voice.description = description ? description : L"";
    voice.gender = token_attribute(token.p, L"Gender");
    voice.culture = parse_language(token_attribute(token.p, L"Language"));
    CoTaskMemFree(id);
    CoTaskMemFree(description);
    result.push_back(std::move(voice));
  }
  std::ranges::sort(result, [](const InstalledVoice &a, const InstalledVoice &b) {
    return _wcsicmp(a.description.c_str(), b.description.c_str()) < 0;
  });
  return result;
}

[[nodiscard]] bool find_voice_token(std::wstring_view voice_id,
                                    ISpObjectToken **out) {
  Com<ISpObjectTokenCategory> category;
  if (FAILED(open_voices_category(category.put()))) return false;
  Com<IEnumSpObjectTokens> tokens;
  if (FAILED(category->EnumTokens(nullptr, nullptr, tokens.put())))
    return false;
  const std::wstring wanted(voice_id);
  for (;;) {
    ISpObjectToken *raw = nullptr;
    ULONG fetched = 0;
    if (FAILED(tokens->Next(1, &raw, &fetched)) || fetched == 0 || !raw)
      break;
    Com<ISpObjectToken> token;
    token.p = raw;
    LPWSTR id = nullptr;
    if (FAILED(token->GetId(&id)) || !id) continue;
    const std::wstring candidate = id;
    CoTaskMemFree(id);
    if (_wcsicmp(candidate.c_str(), wanted.c_str()) == 0) {
      *out = token.p;
      token.p = nullptr;
      return true;
    }
  }
  return false;
}

} // namespace

class SapiSpeechBackend final : public INativeSpeechBackend {
public:
  explicit SapiSpeechBackend(std::string fallback_detail)
      : detail_(std::move(fallback_detail)) {
    // Discovery runs on its own STA so a misbehaving voice cannot poison the
    // long-lived worker (matching the reference's discovery thread).
    std::thread discovery([&] {
      if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        voices_ = discover_voices();
        CoUninitialize();
      }
      voices_pending_.store(false, std::memory_order_release);
    });
    if (!discovery.joinable()) {
      voices_pending_ = false;
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while (voices_pending_.load(std::memory_order_acquire)) {
      if (std::chrono::steady_clock::now() > deadline) {
        discovery.detach();
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    discovery.join();
    if (voices_.empty()) {
      detail_ = "No installed SAPI voices were found.";
      return;
    }
    worker_ = std::thread([this] { run(); });
  }

  ~SapiSpeechBackend() override {
    {
      std::lock_guard guard(mutex_);
      stopping_ = true;
      // Dropped promises resolve with broken_promise to their futures.
      queue_.clear();
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  bool available() const noexcept override { return !voices_.empty(); }
  std::string backend_id() const override { return "windows-sapi"; }
  std::string detail() const override {
    return voices_.empty() ? detail_
                           : std::to_string(voices_.size()) + " voices";
  }
  std::vector<std::string> voices() const override {
    std::vector<std::string> result;
    result.reserve(voices_.size());
    for (const auto &voice : voices_) result.push_back(narrow(voice.description));
    return result;
  }

  std::string resolve_voice_id(const NativeVoiceProfile &profile,
                               std::string_view culture) const override {
    if (voices_.empty()) return {};
    const auto preferred = wide(profile.preferred_voice.value_or(""));
    if (!preferred.empty()) {
      for (const auto &voice : voices_) {
        const auto &id = voice.id;
        const auto &description = voice.description;
        if (_wcsicmp(id.c_str(), preferred.c_str()) == 0 ||
            _wcsicmp(description.c_str(), preferred.c_str()) == 0 ||
            (description.size() >= preferred.size() &&
             std::search(description.begin(), description.end(),
                         preferred.begin(), preferred.end(),
                         [](wchar_t a, wchar_t b) {
                           return std::towlower(a) == std::towlower(b);
                         }) != description.end()))
          return narrow(id);
      }
    }
    std::wstring gender = wide(profile.sex);
    if (gender.empty()) {
      const auto presentation = wide(profile.presentation);
      auto lowered = presentation;
      std::ranges::transform(lowered, lowered.begin(),
                             [](wchar_t c) { return std::towlower(c); });
      if (lowered.find(L"female") != std::wstring::npos) gender = L"female";
      else if (lowered.find(L"male") != std::wstring::npos) gender = L"male";
    }
    const auto desired = wide(std::string(culture));
    const auto best = *std::ranges::max_element(
        voices_,
        [&](const InstalledVoice &a, const InstalledVoice &b) {
          const auto score = [&](const InstalledVoice &voice) {
            return (!gender.empty() &&
                    _wcsicmp(voice.gender.c_str(), gender.c_str()) == 0
                        ? 2
                        : 0) +
                   (!desired.empty() &&
                            _wcsnicmp(voice.culture.c_str(), desired.c_str(),
                                      desired.size()) == 0
                        ? 1
                        : 0);
          };
          return score(a) < score(b);
        });
    return narrow(best.id);
  }

  std::future<NativeVoiceResult>
  synthesize(const NativeVoiceProfile &profile, std::string normalized_text,
             std::filesystem::path wav_path) override {
    std::promise<NativeVoiceResult> promise;
    auto future = promise.get_future();
    Work work;
    work.rate = std::clamp(static_cast<int>(std::lround(profile.rate)), -10, 10);
    work.voice_id = wide(resolve_voice_id(profile, profile.culture));
    work.text = wide(normalized_text);
    work.target = std::move(wav_path);
    work.generation = generation_.load();
    {
      std::lock_guard guard(mutex_);
      if (stopping_ || !worker_.joinable()) {
        promise.set_value(NativeVoiceResult{false, {}, detail_, false, {}});
        return future;
      }
      work.completion = std::move(promise);
      queue_.push_back(std::move(work));
    }
    condition_.notify_one();
    return future;
  }

  void clear_pending() override {
    ++generation_;
    std::lock_guard guard(mutex_);
    queue_.clear();
  }

private:
  struct Work {
    std::promise<NativeVoiceResult> completion;
    std::wstring voice_id, text;
    std::filesystem::path target;
    int rate{};
    std::uint64_t generation{};
    Work() = default;
    Work(Work &&) = default;
    Work &operator=(Work &&) = default;
  };

  std::atomic<bool> voices_pending_{true};
  std::atomic<std::uint64_t> generation_{0};
  std::vector<InstalledVoice> voices_;
  std::string detail_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Work> queue_;
  bool stopping_{};

  void run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
      Work work;
      {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
        if (stopping_) break;
        work = std::move(queue_.front());
        queue_.pop_front();
      }
      NativeVoiceResult result;
      result.selected_voice = narrow(work.voice_id);
      if (work.generation != generation_.load()) {
        result.error = "cancelled";
        work.completion.set_value(std::move(result));
        continue;
      }
      try {
        synthesize_on_sta(work);
        if (work.generation != generation_.load()) {
          std::error_code error;
          std::filesystem::remove(work.target, error);
          result.error = "cancelled";
        } else {
          result.succeeded = true;
          result.wave_path = work.target;
          result.cache_hit = false;
        }
      } catch (const std::exception &error) {
        result.error = error.what();
      }
      work.completion.set_value(std::move(result));
    }
    CoUninitialize();
  }

  void synthesize_on_sta(const Work &work) {
    Com<ISpVoice> voice;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                IID_ISpVoice, reinterpret_cast<void **>(voice.put()))))
      throw std::runtime_error("SAPI.SpVoice could not be created.");
    if (!work.voice_id.empty()) {
      Com<ISpObjectToken> token;
      if (find_voice_token(work.voice_id, token.put()) && token)
        voice->SetVoice(token.p);
    }
    voice->SetRate(work.rate);
    Com<ISpStreamFormat> format;
    if (FAILED(CoCreateInstance(CLSID_SpAudioFormat, nullptr, CLSCTX_ALL,
                                IID_ISpStreamFormat,
                                reinterpret_cast<void **>(format.put()))))
      throw std::runtime_error("SAPI.SpAudioFormat could not be created.");
    // The Type property is only exposed on the automation dual interface.
    Com<ISpeechAudioFormat> automation;
    if (FAILED(format.p->QueryInterface(IID_ISpeechAudioFormat,
                                        reinterpret_cast<void **>(
                                            automation.put()))) ||
        FAILED(automation->put_Type(SAFT22kHz16BitMono)))
      throw std::runtime_error("SAPI.SpAudioFormat rejected the WAV format.");
    GUID format_id{};
    WAVEFORMATEX *wave_format = nullptr;
    if (FAILED(format->GetFormat(&format_id, &wave_format)) || !wave_format)
      throw std::runtime_error("SAPI.SpAudioFormat could not produce a WAV format.");
    Com<ISpStream> stream;
    if (FAILED(CoCreateInstance(CLSID_SpFileStream, nullptr, CLSCTX_ALL,
                                IID_ISpStream,
                                reinterpret_cast<void **>(stream.put())))) {
      CoTaskMemFree(wave_format);
      throw std::runtime_error("SAPI.SpFileStream could not be created.");
    }
    // SpFileStream rejects long paths; synthesize in %TEMP% and move.
    wchar_t temp_dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_dir);
    const std::wstring sapi_path =
        std::wstring(temp_dir) + L"stellar-voice-" +
        std::to_wstring(
            std::hash<std::string>{}(narrow(work.voice_id) + work.target.string() +
                                     std::to_string(std::rand()))) +
        L".wav";
    const HRESULT bound =
        stream->BindToFile(sapi_path.c_str(), SPFM_CREATE_ALWAYS, &format_id,
                           wave_format, 0);
    CoTaskMemFree(wave_format);
    if (FAILED(bound))
      throw std::runtime_error("SAPI.SpFileStream could not open the WAV path.");
    if (FAILED(voice->SetOutput(stream.p, TRUE)))
      throw std::runtime_error("SAPI voice rejected the file stream.");
    if (FAILED(voice->Speak(work.text.c_str(), SVSFlagsAsync | SVSFIsNotXML,
                            nullptr)))
      throw std::runtime_error("SAPI voice rejected the line.");
    const auto started = std::chrono::steady_clock::now();
    while (!voice->WaitUntilDone(50)) {
      if (work.generation != generation_.load()) {
        voice->Speak(L"", SVSFlagsAsync | SVSFPurgeBeforeSpeak | SVSFIsNotXML,
                     nullptr);
        throw std::runtime_error("cancelled");
      }
      if (std::chrono::steady_clock::now() - started >
          std::chrono::seconds(30)) {
        voice->Speak(L"", SVSFlagsAsync | SVSFPurgeBeforeSpeak | SVSFIsNotXML,
                     nullptr);
        throw std::runtime_error(
            "Windows SAPI synthesis exceeded 30 seconds.");
      }
    }
    // Detach the output before releasing the stream so the file is closed
    // before the move.
    voice->SetOutput(nullptr, FALSE);
    stream.p->Release();
    stream.p = nullptr;
    if (!NativeVoiceCache::is_valid_wave(sapi_path))
      throw std::runtime_error("SAPI did not produce a valid PCM WAV file.");
    std::error_code error;
    std::filesystem::create_directories(work.target.parent_path(), error);
    if (std::filesystem::exists(work.target))
      std::filesystem::remove(work.target, error);
    if (!MoveFileW(sapi_path.c_str(), work.target.wstring().c_str()))
      throw std::runtime_error("The synthesized WAV could not be moved.");
  }
};

class UnavailableSpeechBackend final : public INativeSpeechBackend {
public:
  explicit UnavailableSpeechBackend(std::string detail)
      : detail_(std::move(detail)) {}
  bool available() const noexcept override { return false; }
  std::string backend_id() const override { return "windows-sapi"; }
  std::string detail() const override { return detail_; }
  std::vector<std::string> voices() const override { return {}; }
  std::string resolve_voice_id(const NativeVoiceProfile &,
                               std::string_view) const override {
    return {};
  }
  std::future<NativeVoiceResult>
  synthesize(const NativeVoiceProfile &, std::string,
             std::filesystem::path) override {
    std::promise<NativeVoiceResult> promise;
    promise.set_value(NativeVoiceResult{false, {}, detail_, false, {}});
    return promise.get_future();
  }
  void clear_pending() override {}

private:
  std::string detail_;
};

#else

class UnavailableSpeechBackend final : public INativeSpeechBackend {
public:
  bool available() const noexcept override { return false; }
  std::string backend_id() const override { return "windows-sapi"; }
  std::string detail() const override {
    return "Windows SAPI is unavailable on this platform.";
  }
  std::vector<std::string> voices() const override { return {}; }
  std::string resolve_voice_id(const NativeVoiceProfile &,
                               std::string_view) const override { return {}; }
  std::future<NativeVoiceResult>
  synthesize(const NativeVoiceProfile &, std::string,
             std::filesystem::path) override {
    std::promise<NativeVoiceResult> promise;
    promise.set_value(NativeVoiceResult{false, {},
                                        "Windows SAPI is unavailable on this "
                                        "platform.",
                                        false, {}});
    return promise.get_future();
  }
  void clear_pending() override {}
};

#endif

std::unique_ptr<INativeSpeechBackend> create_offline_speech_backend() {
#ifdef _WIN32
  try {
    return std::make_unique<SapiSpeechBackend>(
        "Offline system speech via Windows SAPI.");
  } catch (const std::exception &error) {
    return std::make_unique<UnavailableSpeechBackend>(error.what());
  }
#else
  return std::make_unique<UnavailableSpeechBackend>();
#endif
}

} // namespace stellar::native_voice
