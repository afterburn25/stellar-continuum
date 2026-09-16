#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_image_preparation.hpp>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

namespace stellar::native_surface_ui {

// App-owned immutable source art. One approved albedo is intentionally shared
// by every surface; the workspace applies a neutral treatment because its view
// intentionally carries no environment-specific presentation facts.
class NativeSurfaceArtAssets final {
 public:
  static constexpr std::size_t maximum_cached_bytes = 16u * 1024u * 1024u;

  explicit NativeSurfaceArtAssets(std::filesystem::path asset_root);
  NativeSurfaceArtAssets(const NativeSurfaceArtAssets&) = delete;
  NativeSurfaceArtAssets& operator=(const NativeSurfaceArtAssets&) = delete;

  // The app supplies its shared preparation queue. `request_image` never
  // decodes synchronously: it admits one bounded job and later polls it.
  void use_background_preparation(
      std::shared_ptr<stellar::native_map::ImagePreparationQueue>);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  request_image();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  image() const;
  [[nodiscard]] std::size_t cache_bytes() const noexcept;
  [[nodiscard]] std::size_t decode_count() const noexcept;

 private:
  void require_owner() const;
  void collect_ready();

  std::thread::id owner_{std::this_thread::get_id()};
  std::filesystem::path source_path_;
  std::shared_ptr<stellar::native_map::ImagePreparationQueue> preparation_;
  std::optional<stellar::native_map::ImagePreparationQueue::Ticket> pending_;
  std::shared_ptr<const stellar::native_map::RgbaImage> image_;
  std::exception_ptr failure_;
  std::size_t decode_count_{};
};

} // namespace stellar::native_surface_ui
