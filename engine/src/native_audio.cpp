#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/native_audio.hpp>

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#else
#error The native audio backend requires Windows Media Foundation.
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace stellar::engine::audio {
namespace {
constexpr std::size_t bytes_per_frame = sizeof(float) * audio_channels;
constexpr std::size_t music_queue_limit = static_cast<std::size_t>(audio_sample_rate) * bytes_per_frame * 3u / 4u;
constexpr std::size_t voice_queue_limit = music_queue_limit;
constexpr std::size_t music_chunk_bytes = 32u * 1024u;
constexpr float voice_music_duck = 0.55f;
constexpr std::size_t maximum_effect_voices = 8;

[[nodiscard]] std::string utf8_path(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::runtime_error sdl_error(const char* operation) {
  return std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

void require_sdl(bool success, const char* operation) {
  if (!success) throw sdl_error(operation);
}

[[nodiscard]] std::runtime_error hresult_error(const char* operation, HRESULT result,
                                                const std::filesystem::path& path) {
  char code[16]{};
  std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
  return std::runtime_error(std::string(operation) + " failed (HRESULT " + code + ") for " + utf8_path(path));
}

void require_hresult(HRESULT result, const char* operation, const std::filesystem::path& path) {
  if (FAILED(result)) throw hresult_error(operation, result, path);
}

class ComApartment final {
 public:
  explicit ComApartment(const std::filesystem::path& path) : path_(path) {
    result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (result_ == RPC_E_CHANGED_MODE) {
      // The thread already belongs to a different apartment — SDL
      // STA-initializes the main thread, and stream decoding binds lazily
      // on whichever thread first calls read(). Synchronous SourceReader
      // use is apartment-agnostic, so borrow the existing apartment; a
      // failed CoInitializeEx added no reference and must not be balanced
      // by CoUninitialize here.
    } else {
      require_hresult(result_, "CoInitializeEx", path_);
      initialized_ = true;
    }
    const auto media_result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(media_result)) {
      if (initialized_) {
        CoUninitialize();
        initialized_ = false;
      }
      throw hresult_error("MFStartup", media_result, path_);
    }
    media_started_ = true;
  }
  ~ComApartment() {
    if (media_started_) (void)MFShutdown();
    if (initialized_) CoUninitialize();
  }
  ComApartment(const ComApartment&) = delete;
  ComApartment& operator=(const ComApartment&) = delete;
 private:
  const std::filesystem::path& path_;
  HRESULT result_{};
  bool initialized_{};
  bool media_started_{};
};

void validate_samples(const std::vector<float>& samples) {
  if (samples.empty() || samples.size() % audio_channels != 0) {
    throw std::invalid_argument("Audio clips require non-empty interleaved stereo samples.");
  }
  if (samples.size() > maximum_decoded_audio_bytes / sizeof(float)) {
    throw std::length_error("Audio clip exceeds the 96 MiB decoded PCM limit.");
  }
  for (const float value : samples) {
    if (!std::isfinite(value) || value < -1.0f || value > 1.0f) {
      throw std::invalid_argument("Audio clip samples must be finite and normalized to [-1, 1].");
    }
  }
}

[[nodiscard]] SDL_AudioStream* make_stream(SDL_AudioDeviceID device) {
  const SDL_AudioSpec spec{SDL_AUDIO_F32, audio_channels, audio_sample_rate};
  auto* stream = SDL_CreateAudioStream(&spec, &spec);
  if (!stream) throw sdl_error("SDL audio stream creation failed");
  try {
    require_sdl(SDL_BindAudioStream(device, stream), "SDL audio stream binding failed");
  } catch (...) {
    SDL_DestroyAudioStream(stream);
    throw;
  }
  return stream;
}

} // namespace

AudioClip::AudioClip(std::vector<float> samples) noexcept : samples_(std::move(samples)) {}

std::shared_ptr<const AudioClip> AudioClip::create(std::vector<float> samples) {
  validate_samples(samples);
  return std::shared_ptr<const AudioClip>(new AudioClip(std::move(samples)));
}

std::span<const float> AudioClip::samples() const noexcept { return samples_; }
std::size_t AudioClip::byte_size() const noexcept { return samples_.size() * sizeof(float); }
std::uint64_t AudioClip::sample_frames() const noexcept { return samples_.size() / audio_channels; }

namespace {

constexpr DWORD audio_reader_stream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

// Requests canonical 48 kHz interleaved stereo float PCM from a reader
// and verifies the negotiated type — the SourceReader inserts whichever
// decoder/resampler chain the source format requires.
void configure_pcm_output(IMFSourceReader* reader,
                          const std::filesystem::path& path) {
  using Microsoft::WRL::ComPtr;
  ComPtr<IMFMediaType> output_type;
  require_hresult(MFCreateMediaType(output_type.GetAddressOf()), "MFCreateMediaType", path);
  require_hresult(output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set audio major type", path);
  require_hresult(output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float), "Set audio float subtype", path);
  require_hresult(output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels), "Set audio channel count", path);
  require_hresult(output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_sample_rate), "Set audio sample rate", path);
  require_hresult(output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32), "Set audio sample width", path);
  require_hresult(reader->SetCurrentMediaType(audio_reader_stream, nullptr, output_type.Get()),
                  "Configure audio decoder", path);
  ComPtr<IMFMediaType> negotiated_type;
  require_hresult(reader->GetCurrentMediaType(audio_reader_stream, negotiated_type.GetAddressOf()),
                  "Inspect negotiated audio type", path);
  GUID subtype{};
  UINT32 channels{}, rate{}, bits{};
  require_hresult(negotiated_type->GetGUID(MF_MT_SUBTYPE, &subtype), "Inspect negotiated audio subtype", path);
  require_hresult(negotiated_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "Inspect negotiated audio channels", path);
  require_hresult(negotiated_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate), "Inspect negotiated audio rate", path);
  require_hresult(negotiated_type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits), "Inspect negotiated audio width", path);
  if (subtype != MFAudioFormat_Float || channels != audio_channels || rate != audio_sample_rate || bits != 32) {
    throw std::runtime_error("Media Foundation did not negotiate 48 kHz stereo float PCM for " + utf8_path(path));
  }
}

std::shared_ptr<const AudioClip> decode_audio_clip_impl(
    std::span<const std::uint8_t> encoded, const std::filesystem::path& path) {
  if(encoded.empty())throw std::runtime_error("Empty audio asset");
  ComApartment apartment(path);
  using Microsoft::WRL::ComPtr;
  ComPtr<IMFAttributes> reader_attributes;
  require_hresult(MFCreateAttributes(reader_attributes.GetAddressOf(), 1), "Create audio reader attributes", path);
  // SourceReader inserts the audio decoder/resampler needed to honor the requested PCM type.
  require_hresult(reader_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
                  "Configure audio reader transforms", path);
  ComPtr<IMFSourceReader> reader;
  ComPtr<IStream> source_stream;require_hresult(CreateStreamOnHGlobal(nullptr,TRUE,source_stream.GetAddressOf()),"Create audio memory stream",path);
  ULONG written{};require_hresult(source_stream->Write(encoded.data(),static_cast<ULONG>(encoded.size()),&written),"Write audio memory stream",path);
  if(written!=encoded.size())throw std::runtime_error("Incomplete audio memory stream");
  LARGE_INTEGER zero{};require_hresult(source_stream->Seek(zero,STREAM_SEEK_SET,nullptr),"Seek audio stream",path);
  ComPtr<IMFByteStream> byte_stream;require_hresult(MFCreateMFByteStreamOnStream(source_stream.Get(),byte_stream.GetAddressOf()),"Create audio byte stream",path);
  require_hresult(MFCreateSourceReaderFromByteStream(byte_stream.Get(),reader_attributes.Get(),reader.GetAddressOf()),"Decode cooked audio",path);
  constexpr DWORD audio_stream = audio_reader_stream;
  configure_pcm_output(reader.Get(), path);

  std::vector<float> decoded;
  decoded.reserve(std::min<std::size_t>(maximum_decoded_audio_bytes / sizeof(float), 262144u));
  for (;;) {
    DWORD flags{};
    ComPtr<IMFSample> sample;
    require_hresult(reader->ReadSample(audio_stream, 0, nullptr, &flags, nullptr,
                                       sample.GetAddressOf()), "Read audio sample", path);
    if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
                 MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
      throw std::runtime_error("Media Foundation changed or rejected the negotiated audio format for " + utf8_path(path));
    }
    if (sample) {
      ComPtr<IMFMediaBuffer> buffer;
      require_hresult(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()), "Flatten audio sample", path);
      BYTE* bytes{};
      DWORD length{};
      require_hresult(buffer->Lock(&bytes, nullptr, &length), "Lock audio sample", path);
      struct BufferUnlock final {
        IMFMediaBuffer* buffer;
        ~BufferUnlock() { (void)buffer->Unlock(); }
      } unlock{buffer.Get()};
      if (length % bytes_per_frame != 0 || length > maximum_decoded_audio_bytes ||
          decoded.size() > (maximum_decoded_audio_bytes - length) / sizeof(float)) {
        throw std::length_error("Decoded audio exceeds the 96 MiB stereo PCM limit: " + utf8_path(path));
      }
      const auto old_size = decoded.size();
      const auto required_samples = old_size + length / sizeof(float);
      if (required_samples > decoded.capacity()) {
        const auto grown_samples = std::min(maximum_decoded_audio_bytes / sizeof(float),
                                             std::max(required_samples, decoded.capacity() * 2u));
        decoded.reserve(grown_samples);
      }
      decoded.resize(required_samples);
      std::memcpy(decoded.data() + old_size, bytes, length);
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
  }
  // Media Foundation's float output is normally normalized. Clamp tiny codec overshoots.
  for (float& sample : decoded) {
    if (!std::isfinite(sample)) throw std::runtime_error("Decoded audio contains a non-finite sample: " + utf8_path(path));
    sample = std::clamp(sample, -1.0f, 1.0f);
  }
  return AudioClip::create(std::move(decoded));
}

} // namespace

std::shared_ptr<const AudioClip> decode_audio_clip(const std::filesystem::path& path) {
  return decode_audio_clip_impl(
      read_resource(path, maximum_source_audio_bytes), path);
}

std::shared_ptr<const AudioClip> decode_audio_clip(
    std::span<const std::uint8_t> encoded,
    const std::filesystem::path& label) {
  return decode_audio_clip_impl(encoded, label);
}

struct AudioStreamDecoder::Storage {
  std::filesystem::path path;
  // The Media Foundation pipeline binds lazily on first read() so the
  // reading thread owns the COM apartment — creation on a worker
  // thread must not pin the reader to it.
  std::optional<ComApartment> apartment;
  Microsoft::WRL::ComPtr<IMFSourceReader> reader;
  // Whole-frame tail of a ReadSample that overran the caller's span.
  std::vector<float> pending;
  std::size_t pending_offset{};
  bool bound{};
  bool finished{};

  void bind() {
    if (bound) return;
    apartment.emplace(path);
    using Microsoft::WRL::ComPtr;
    ComPtr<IMFAttributes> attributes;
    require_hresult(MFCreateAttributes(attributes.GetAddressOf(), 1),
                    "Create audio reader attributes", path);
    require_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
                    "Configure audio reader transforms", path);
    require_hresult(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(),
                                              reader.GetAddressOf()),
                    "Open audio stream", path);
    configure_pcm_output(reader.Get(), path);
    bound = true;
  }
};

AudioStreamDecoder::AudioStreamDecoder(std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}

AudioStreamDecoder::~AudioStreamDecoder() = default;

std::size_t AudioStreamDecoder::read(std::span<float> out) {
  if (!storage_) throw std::logic_error("AudioStreamDecoder read on a moved object");
  if (out.size() % audio_channels != 0)
    throw std::invalid_argument("Audio stream reads must request whole stereo frames");
  try {
    auto &s = *storage_;
    s.bind();
  std::size_t written = 0;
  while (written < out.size() && !s.finished) {
    if (s.pending_offset < s.pending.size()) {
      const auto count =
          std::min(out.size() - written, s.pending.size() - s.pending_offset);
      std::memcpy(out.data() + written, s.pending.data() + s.pending_offset,
                  count * sizeof(float));
      s.pending_offset += count;
      written += count;
      if (s.pending_offset == s.pending.size()) {
        s.pending.clear();
        s.pending_offset = 0;
      }
      continue;
    }
    DWORD flags{};
    Microsoft::WRL::ComPtr<IMFSample> sample;
    require_hresult(s.reader->ReadSample(audio_reader_stream, 0, nullptr, &flags,
                                       nullptr, sample.GetAddressOf()),
                    "Read audio sample", s.path);
    if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
                 MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
      throw std::runtime_error("Media Foundation changed or rejected the negotiated audio format for " + utf8_path(s.path));
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) s.finished = true;
    if (!sample) continue;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    require_hresult(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()),
                    "Flatten audio sample", s.path);
    BYTE* bytes{};
    DWORD length{};
    require_hresult(buffer->Lock(&bytes, nullptr, &length), "Lock audio sample", s.path);
    struct BufferUnlock final {
      IMFMediaBuffer* buffer;
      ~BufferUnlock() { (void)buffer->Unlock(); }
    } unlock{buffer.Get()};
    if (length % bytes_per_frame != 0)
      throw std::runtime_error("Audio stream delivered a partial frame for " + utf8_path(s.path));
    const auto count = static_cast<std::size_t>(length) / sizeof(float);
    const auto* floats = reinterpret_cast<const float*>(bytes);
    const auto direct = std::min(count, out.size() - written);
    std::memcpy(out.data() + written, floats, direct * sizeof(float));
    written += direct;
    if (direct < count)
      s.pending.assign(floats + direct, floats + count);
    for (std::size_t i = written - direct; i < written; ++i) {
      if (!std::isfinite(out[i]))
        throw std::runtime_error("Decoded audio contains a non-finite sample: " + utf8_path(s.path));
      out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
    for (float& value : s.pending) {
      if (!std::isfinite(value))
        throw std::runtime_error("Decoded audio contains a non-finite sample: " + utf8_path(s.path));
      value = std::clamp(value, -1.0f, 1.0f);
    }
  }
    return written;
  } catch (const std::exception& error) {
    throw AudioStreamError(error.what());
  }
}

void AudioStreamDecoder::rewind() {
  if (!storage_) throw std::logic_error("AudioStreamDecoder rewind on a moved object");
  try {
    auto &s = *storage_;
    s.bind();
    PROPVARIANT position{};
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = 0;
    require_hresult(s.reader->SetCurrentPosition(GUID_NULL, position),
                    "Rewind audio stream", s.path);
    PropVariantClear(&position);
    s.pending.clear();
    s.pending_offset = 0;
    s.finished = false;
  } catch (const std::exception& error) {
    throw AudioStreamError(error.what());
  }
}

bool AudioStreamDecoder::finished() const noexcept {
  return storage_ && storage_->finished;
}

std::shared_ptr<AudioStreamDecoder>
open_audio_stream(const std::filesystem::path& path) {
  if (path.empty()) throw std::invalid_argument("Audio stream path is empty");
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error)
    throw AudioStreamError("Audio stream source is missing: " + utf8_path(path));
  auto storage = std::make_unique<AudioStreamDecoder::Storage>();
  storage->path = path;
  return std::shared_ptr<AudioStreamDecoder>(
      new AudioStreamDecoder(std::move(storage)));
}

struct AudioOutput::Storage {
  struct Voice { SDL_AudioStream* stream{}; std::shared_ptr<const AudioClip> clip; std::uint64_t age{}; };
  SDL_AudioDeviceID device{};
  SDL_AudioStream* music_stream{};
  SDL_AudioStream* voice_stream{};
  std::array<Voice, maximum_effect_voices> effects{};
  std::shared_ptr<const AudioClip> music_clip;
  std::shared_ptr<AudioStreamDecoder> music_decoder;
  std::shared_ptr<const AudioClip> voice_clip;
  std::size_t music_offset{};
  std::size_t voice_offset{};
  std::uint64_t next_age{};
  std::uint64_t effect_play_count{};
  std::uint64_t voice_play_count{};
  std::array<std::byte, music_chunk_bytes> music_scratch{};
  std::vector<float> pan_scratch;
  float master{0.78f};
  float music{0.64f};
  float effects_gain{0.82f};
  float voice_gain{0.82f};
  bool audio_initialized{};
  bool music_started{};
  bool voice_flushed{};
};

void AudioOutput::require_owner() const {
  if (std::this_thread::get_id() != owner_) throw std::logic_error("AudioOutput must be used from its creating thread.");
}

AudioOutput::AudioOutput() : owner_(std::this_thread::get_id()), storage_(std::make_unique<Storage>()) {
  try {
    require_sdl(SDL_InitSubSystem(SDL_INIT_AUDIO), "SDL audio subsystem initialization failed");
    storage_->audio_initialized = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, audio_channels, audio_sample_rate};
    storage_->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!storage_->device) throw sdl_error("SDL default playback device open failed");
    storage_->music_stream = make_stream(storage_->device);
    storage_->voice_stream = make_stream(storage_->device);
    for (auto& voice : storage_->effects) voice.stream = make_stream(storage_->device);
    set_volumes(storage_->master, storage_->music, storage_->effects_gain);
  } catch (...) {
    cleanup_unchecked();
    throw;
  }
}

AudioOutput::~AudioOutput() noexcept { cleanup_unchecked(); }

void AudioOutput::cleanup_unchecked() noexcept {
  if (!storage_) return;
  for (auto& voice : storage_->effects) {
    voice.clip.reset();
    if (voice.stream) { SDL_ClearAudioStream(voice.stream); SDL_DestroyAudioStream(voice.stream); voice.stream = nullptr; }
  }
  storage_->voice_clip.reset();
  if (storage_->voice_stream) { SDL_ClearAudioStream(storage_->voice_stream); SDL_DestroyAudioStream(storage_->voice_stream); storage_->voice_stream = nullptr; }
  storage_->music_clip.reset();
  storage_->music_decoder.reset();
  if (storage_->music_stream) { SDL_ClearAudioStream(storage_->music_stream); SDL_DestroyAudioStream(storage_->music_stream); storage_->music_stream = nullptr; }
  if (storage_->device) { SDL_CloseAudioDevice(storage_->device); storage_->device = 0; }
  if (storage_->audio_initialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); storage_->audio_initialized = false; }
}

void AudioOutput::set_volumes(float master, float music, float effects) {
  require_owner();
  const auto valid = [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
  if (!valid(master) || !valid(music) || !valid(effects)) throw std::invalid_argument("Audio gains must be finite values in [0, 1].");
  storage_->master = master; storage_->music = music; storage_->effects_gain = effects;
  apply_gains();
}
void AudioOutput::set_voice_gain(float voice) {
  require_owner();
  if (!std::isfinite(voice) || voice < 0.f || voice > 1.f)
    throw std::invalid_argument("Voice gain must be finite values in [0, 1].");
  storage_->voice_gain = voice;
  apply_gains();
}

void AudioOutput::apply_gains() {
  const auto music_duck = storage_->voice_clip ? voice_music_duck : 1.0f;
  require_sdl(SDL_SetAudioStreamGain(storage_->music_stream, storage_->master * storage_->music * music_duck),
              "SDL music gain setup failed");
  require_sdl(SDL_SetAudioStreamGain(storage_->voice_stream, storage_->master * storage_->voice_gain),
              "SDL voice gain setup failed");
  for (const auto& voice : storage_->effects) {
    require_sdl(SDL_SetAudioStreamGain(voice.stream, storage_->master * storage_->effects_gain),
                "SDL effect gain setup failed");
  }
}

void AudioOutput::play_music(std::shared_ptr<const AudioClip> clip) {
  require_owner();
  if (!clip) throw std::invalid_argument("Music playback requires an audio clip.");
  if (storage_->music_started && storage_->music_clip == clip) return;
  require_sdl(SDL_ClearAudioStream(storage_->music_stream), "SDL music queue clear failed");
  storage_->music_clip = std::move(clip);
  storage_->music_decoder.reset();
  storage_->music_offset = 0;
  storage_->music_started = true;
  service();
}

void AudioOutput::play_music(std::shared_ptr<AudioStreamDecoder> decoder) {
  require_owner();
  if (!decoder) throw std::invalid_argument("Music playback requires an audio stream decoder.");
  if (storage_->music_started && storage_->music_decoder == decoder) return;
  require_sdl(SDL_ClearAudioStream(storage_->music_stream), "SDL music queue clear failed");
  storage_->music_clip.reset();
  storage_->music_decoder = std::move(decoder);
  storage_->music_offset = 0;
  storage_->music_started = true;
  service();
}

void AudioOutput::stop_music() {
  require_owner();
  require_sdl(SDL_ClearAudioStream(storage_->music_stream), "SDL music queue clear failed");
  storage_->music_clip.reset(); storage_->music_decoder.reset();
  storage_->music_offset = 0; storage_->music_started = false;
}

void AudioOutput::play_effect(std::shared_ptr<const AudioClip> clip) {
  play_effect(std::move(clip), 0.0f);
}

void AudioOutput::play_effect(std::shared_ptr<const AudioClip> clip, float pan) {
  play_effect(std::move(clip), pan, 1.0f);
}

void AudioOutput::play_effect(std::shared_ptr<const AudioClip> clip, float pan, float gain) {
  require_owner();
  if (!clip) throw std::invalid_argument("Effect playback requires an audio clip.");
  if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f)
    throw std::invalid_argument("Effect pan must be a finite value in [-1, 1].");
  if (!std::isfinite(gain) || gain < 0.0f || gain > 1.0f)
    throw std::invalid_argument("Effect gain must be a finite value in [0, 1].");
  if (clip->byte_size() > maximum_effect_audio_bytes) {
    throw std::length_error("Audio effect exceeds the 1 MiB per-voice limit.");
  }
  auto* selected = &storage_->effects.front();
  for (auto& voice : storage_->effects) if (!voice.clip) { selected = &voice; break; }
  if (selected->clip) for (auto& voice : storage_->effects) if (voice.age < selected->age) selected = &voice;
  require_sdl(SDL_ClearAudioStream(selected->stream), "SDL effect queue clear failed");
  if (pan == 0.0f && gain == 1.0f) {
    require_sdl(SDL_PutAudioStreamData(selected->stream, clip->samples().data(), static_cast<int>(clip->byte_size())), "SDL effect queue failed");
  } else {
    // Equal-power stereo pan and distance gain applied at queue time —
    // the SDL stream gain is scalar, so per-channel weighting happens on
    // the PCM.
    const float angle = (pan + 1.0f) * 0.7853981633974483f; // (pan+1) * pi/4
    const float left_gain = std::cos(angle) * gain, right_gain = std::sin(angle) * gain;
    storage_->pan_scratch.resize(clip->samples().size());
    const auto source = clip->samples();
    for (std::size_t i = 0; i + 1 < source.size(); i += 2) {
      storage_->pan_scratch[i] = source[i] * left_gain;
      storage_->pan_scratch[i + 1] = source[i + 1] * right_gain;
    }
    require_sdl(SDL_PutAudioStreamData(selected->stream, storage_->pan_scratch.data(),
                                       static_cast<int>(clip->byte_size())), "SDL effect queue failed");
  }
  // Effects are finite inputs. Flushing exposes a converter/resampler tail before this voice goes idle.
  require_sdl(SDL_FlushAudioStream(selected->stream), "SDL effect queue flush failed");
  selected->clip = std::move(clip); selected->age = ++storage_->next_age; ++storage_->effect_play_count;
}

void AudioOutput::play_voice(std::shared_ptr<const AudioClip> clip) {
  require_owner();
  if (!clip) throw std::invalid_argument("Voice playback requires an audio clip.");
  if (clip->byte_size() > maximum_voice_audio_bytes) {
    throw std::length_error("Voice clip exceeds the 8 MiB decoded PCM limit.");
  }
  require_sdl(SDL_ClearAudioStream(storage_->voice_stream), "SDL voice queue clear failed");
  storage_->voice_clip = std::move(clip);
  storage_->voice_offset = 0;
  storage_->voice_flushed = false;
  ++storage_->voice_play_count;
  apply_gains();
  service();
}

void AudioOutput::stop_voice() {
  require_owner();
  require_sdl(SDL_ClearAudioStream(storage_->voice_stream), "SDL voice queue clear failed");
  storage_->voice_clip.reset();
  storage_->voice_offset = 0;
  storage_->voice_flushed = false;
  apply_gains();
}

void AudioOutput::service() {
  require_owner();
  for (auto& voice : storage_->effects) {
    const auto queued = SDL_GetAudioStreamQueued(voice.stream);
    const auto available = SDL_GetAudioStreamAvailable(voice.stream);
    if (queued < 0 || available < 0) throw sdl_error("SDL effect queue inspection failed");
    if (queued == 0 && available == 0) voice.clip.reset();
  }
  if (storage_->voice_clip) {
    auto queued = SDL_GetAudioStreamQueued(storage_->voice_stream);
    auto available = SDL_GetAudioStreamAvailable(storage_->voice_stream);
    if (queued < 0 || available < 0) throw sdl_error("SDL voice queue inspection failed");
    const auto clip_bytes = storage_->voice_clip->byte_size();
    const auto* clip_data = reinterpret_cast<const std::byte*>(storage_->voice_clip->samples().data());
    while (!storage_->voice_flushed && static_cast<std::size_t>(queued) < voice_queue_limit) {
      const auto remaining = clip_bytes - storage_->voice_offset;
      const auto amount = std::min({voice_queue_limit - static_cast<std::size_t>(queued),
                                    music_chunk_bytes, remaining});
      if (amount == 0 || amount % bytes_per_frame != 0) {
        throw std::logic_error("Audio voice clip has invalid frame alignment.");
      }
      require_sdl(SDL_PutAudioStreamData(storage_->voice_stream, clip_data + storage_->voice_offset,
                                         static_cast<int>(amount)), "SDL voice queue failed");
      storage_->voice_offset += amount;
      queued += static_cast<int>(amount);
      if (storage_->voice_offset == clip_bytes) {
        require_sdl(SDL_FlushAudioStream(storage_->voice_stream), "SDL voice queue flush failed");
        storage_->voice_flushed = true;
      }
    }
    queued = SDL_GetAudioStreamQueued(storage_->voice_stream);
    available = SDL_GetAudioStreamAvailable(storage_->voice_stream);
    if (queued < 0 || available < 0) throw sdl_error("SDL voice queue inspection failed");
    if (storage_->voice_flushed && queued == 0 && available == 0) {
      storage_->voice_clip.reset();
      storage_->voice_offset = 0;
      storage_->voice_flushed = false;
      apply_gains();
    }
  }
  if (!storage_->music_started || (!storage_->music_clip && !storage_->music_decoder)) return;
  auto queued = SDL_GetAudioStreamQueued(storage_->music_stream);
  if (queued < 0) throw sdl_error("SDL music queue inspection failed");
  while (static_cast<std::size_t>(queued) < music_queue_limit) {
    const auto available = music_queue_limit - static_cast<std::size_t>(queued);
    const auto amount = std::min(available, music_chunk_bytes);
    if (amount == 0 || amount % bytes_per_frame != 0) throw std::logic_error("Audio music clip has invalid frame alignment.");
    if (storage_->music_decoder) {
      auto& decoder = *storage_->music_decoder;
      auto* scratch = reinterpret_cast<float*>(storage_->music_scratch.data());
      const auto frames = amount / bytes_per_frame;
      std::size_t filled{};
      while (filled < frames) {
        const auto got = decoder.read(
            std::span<float>(scratch + filled * audio_channels,
                             (frames - filled) * audio_channels));
        filled += got / audio_channels;
        if (got == 0) {
          if (!decoder.finished())
            throw std::logic_error("Audio stream stalled before end-of-stream.");
          decoder.rewind();
          const auto retry = decoder.read(
              std::span<float>(scratch + filled * audio_channels,
                               (frames - filled) * audio_channels));
          filled += retry / audio_channels;
          // A source that is still empty after a rewind cannot loop —
          // stop feeding rather than spinning silence into the queue.
          if (retry == 0) break;
        }
      }
      if (filled == 0) break;
      require_sdl(SDL_PutAudioStreamData(storage_->music_stream, storage_->music_scratch.data(),
                                         static_cast<int>(filled * bytes_per_frame)), "SDL music queue failed");
      queued += static_cast<int>(filled * bytes_per_frame);
      continue;
    }
    const auto samples = storage_->music_clip->samples();
    const auto clip_bytes = storage_->music_clip->byte_size();
    const auto* clip_data = reinterpret_cast<const std::byte*>(samples.data());
    std::size_t copied{};
    while (copied < amount) {
      const auto remaining = clip_bytes - storage_->music_offset;
      const auto segment = std::min(remaining, amount - copied);
      std::memcpy(storage_->music_scratch.data() + copied, clip_data + storage_->music_offset, segment);
      copied += segment;
      storage_->music_offset = (storage_->music_offset + segment) % clip_bytes;
    }
    require_sdl(SDL_PutAudioStreamData(storage_->music_stream, storage_->music_scratch.data(),
                                       static_cast<int>(amount)), "SDL music queue failed");
    queued += static_cast<int>(amount);
  }
}

void AudioOutput::stop_all() {
  require_owner();
  stop_music();
  stop_voice();
  for (auto& voice : storage_->effects) {
    require_sdl(SDL_ClearAudioStream(voice.stream), "SDL effect queue clear failed");
    voice.clip.reset(); voice.age = 0;
  }
}

AudioDiagnostics AudioOutput::diagnostics() const {
  require_owner();
  const auto queued = SDL_GetAudioStreamQueued(storage_->music_stream);
  if (queued < 0) throw sdl_error("SDL music queue inspection failed");
  std::size_t active{};
  for (const auto& voice : storage_->effects) if (voice.clip) ++active;
  const auto queued_voice = SDL_GetAudioStreamQueued(storage_->voice_stream);
  const auto available_voice = SDL_GetAudioStreamAvailable(storage_->voice_stream);
  if (queued_voice < 0 || available_voice < 0) throw sdl_error("SDL voice queue inspection failed");
  return {static_cast<std::size_t>(queued), music_queue_limit, active, storage_->effect_play_count,
          storage_->music_started, storage_->music_decoder != nullptr,
          storage_->voice_clip != nullptr,
          static_cast<std::size_t>(queued_voice), static_cast<std::size_t>(available_voice),
          voice_queue_limit, storage_->voice_play_count, SDL_GetAudioStreamGain(storage_->music_stream),
          SDL_GetAudioStreamGain(storage_->voice_stream)};
}

} // namespace stellar::engine::audio


