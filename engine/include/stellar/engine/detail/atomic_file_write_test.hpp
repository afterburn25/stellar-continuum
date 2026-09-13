#pragma once

#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
namespace stellar::engine::detail {

enum class AtomicFileWriteFault {
  none,
  partial_write,
  flush,
  pre_replace,
  race_destination,
  ambiguous_replace,
  temporary_collision_once,
  temporary_collision_exhaustion,
  temporary_access_denied,
  zero_write,
};

void set_atomic_file_write_fault(AtomicFileWriteFault fault) noexcept;

} // namespace stellar::engine::detail
#endif
