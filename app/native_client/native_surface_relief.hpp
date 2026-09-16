#pragma once

#include <stellar/engine/native_image_preparation.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace stellar::native_surface {

struct NativeSurfaceReliefIdentity {
  std::uint64_t campaign_generation{};
  int colony_id{}, body_id{};
  bool operator==(const NativeSurfaceReliefIdentity &) const = default;
};

struct NativeSurfaceReliefStats {
  bool requested{}, ready{}, pending{}, deferred{}, failed{};
  std::size_t cache_bytes{}, reserved_bytes{};
  std::uint64_t generated{};
};

// A fixed world-space slope mask. It is deliberately independent of the
// camera and retains no environment presentation data or source artwork.
class NativeSurfaceRelief final {
 public:
  inline static constexpr int image_resolution = 1024;
  inline static constexpr std::size_t image_bytes =
      static_cast<std::size_t>(image_resolution) * image_resolution * 4u;

  void use_background_preparation(
      std::shared_ptr<native_map::ImagePreparationQueue>);
  [[nodiscard]] std::shared_ptr<const native_map::RgbaImage> request(
      NativeSurfaceReliefIdentity);
  void clear() noexcept;
  [[nodiscard]] std::size_t cache_bytes() const noexcept;
  [[nodiscard]] std::size_t pending_count() const noexcept;
  [[nodiscard]] NativeSurfaceReliefStats stats() const noexcept;
  [[nodiscard]] const std::string &error() const noexcept { return failure_; }

 private:
  void require_owner() const;
  void collect_ready();
  void bind(NativeSurfaceReliefIdentity);

  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<NativeSurfaceReliefIdentity> identity_;
  std::shared_ptr<native_map::ImagePreparationQueue> preparation_;
  std::optional<native_map::ImagePreparationQueue::Ticket> pending_;
  std::shared_ptr<const native_map::RgbaImage> image_;
  bool requested_{}, deferred_{}, failed_{};
  std::uint64_t generated_{};
  std::string failure_;
};

} // namespace stellar::native_surface
