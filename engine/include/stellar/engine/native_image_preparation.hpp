#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace stellar::native_map {

// Owner-thread admission and collection; one Engine JobSystem worker prepares CPU images.
// Factories must own immutable inputs and must not access simulation or GPU state.
class ImagePreparationQueue final {
 public:
  static constexpr std::size_t default_max_outstanding_jobs = 16;
  static constexpr std::size_t default_max_reserved_output_bytes = 32u * 1024u * 1024u;

  class Ticket final {
   public:
    Ticket() = default;
    ~Ticket();
    Ticket(Ticket &&) noexcept;
    Ticket &operator=(Ticket &&) noexcept;
    Ticket(const Ticket &) = delete;
    Ticket &operator=(const Ticket &) = delete;
    // Polling never waits for generation. take() requires readiness and the owner thread;
    // it releases admission capacity and rethrows the original preparation error.
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::shared_ptr<const RgbaImage> take();
    // Non-blocking: running work finishes privately and its result is discarded.
    void cancel() noexcept;

   private:
    friend class ImagePreparationQueue;
    struct Cell;
    Ticket(std::shared_ptr<Cell>, std::thread::id);
    std::shared_ptr<Cell> cell_;
    std::thread::id owner_;
  };

  explicit ImagePreparationQueue(
      std::size_t max_outstanding_jobs = default_max_outstanding_jobs,
      std::size_t max_reserved_output_bytes = default_max_reserved_output_bytes);
  // Explicit shutdown joins the worker. Tickets may outlive the queue.
  ~ImagePreparationQueue();
  ImagePreparationQueue(ImagePreparationQueue &&) = delete;
  ImagePreparationQueue &operator=(ImagePreparationQueue &&) = delete;
  ImagePreparationQueue(const ImagePreparationQueue &) = delete;
  ImagePreparationQueue &operator=(const ImagePreparationQueue &) = delete;

  // nullopt means capacity is full: retry in a later frame. Ready but uncollected
  // results still consume capacity. Reservations bound output, not decoder scratch memory.
  [[nodiscard]] std::optional<Ticket> submit(
      std::size_t reserved_output_bytes,
      std::function<std::shared_ptr<const RgbaImage>()> factory);
  [[nodiscard]] std::size_t outstanding_jobs() const;
  [[nodiscard]] std::size_t reserved_bytes() const;

 private:
  struct SharedState;
  void require_owner() const;
  std::thread::id owner_;
  std::shared_ptr<SharedState> state_;
  std::unique_ptr<class EngineJobs> jobs_;
};

} // namespace stellar::native_map
