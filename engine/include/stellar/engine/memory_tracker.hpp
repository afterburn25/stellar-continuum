#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

struct SubsystemMemoryStats {
    std::string name;
    std::uint64_t current_bytes{};
    std::uint64_t reserved_bytes{};
    std::uint64_t high_water_bytes{};
    std::uint64_t allocation_count{};
    std::uint64_t deallocation_count{};
};

struct MemorySnapshot {
    std::vector<SubsystemMemoryStats> subsystems;
    std::uint64_t total_current_bytes{};
    std::uint64_t total_high_water_bytes{};
};

// Subsystem-aware memory accounting. Subsystems register once, then either
// report their own tallies (report) or funnel allocations through
// TrackedAllocator/note_alloc. High-water marks are retained per subsystem.
class MemoryTracker {
public:
    using SubsystemId = std::uint32_t;
    static constexpr SubsystemId invalid_subsystem = ~0u;

    static MemoryTracker& instance();

    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    [[nodiscard]] SubsystemId register_subsystem(std::string_view name);
    void note_alloc(SubsystemId subsystem, std::uint64_t bytes);
    void note_free(SubsystemId subsystem, std::uint64_t bytes);
    // For caches that track their own occupancy: set absolute levels.
    void report(SubsystemId subsystem, std::uint64_t used_bytes,
                std::uint64_t reserved_bytes = 0);

    [[nodiscard]] MemorySnapshot snapshot() const;
    [[nodiscard]] std::string export_json() const;
    [[nodiscard]] std::vector<std::string> overlay_lines(std::size_t max_rows = 12) const;
    void reset_high_water_marks();

private:
    MemoryTracker() = default;

    mutable std::mutex mutex_;
    std::vector<SubsystemMemoryStats> subsystems_;
    std::unordered_map<std::string, SubsystemId> by_name_;
};

// std::allocator adapter that funnels a container's allocations through a
// named MemoryTracker subsystem. Opt-in per container; no global new override.
template <class T>
class TrackedAllocator {
public:
    using value_type = T;

    TrackedAllocator() noexcept : subsystem_(MemoryTracker::invalid_subsystem) {}
    explicit TrackedAllocator(MemoryTracker::SubsystemId subsystem) noexcept : subsystem_(subsystem) {}
    template <class U>
    TrackedAllocator(const TrackedAllocator<U>& other) noexcept : subsystem_(other.subsystem_) {}

    [[nodiscard]] T* allocate(std::size_t count) {
        T* result = std::allocator<T>{}.allocate(count);
        if (subsystem_ != MemoryTracker::invalid_subsystem)
            MemoryTracker::instance().note_alloc(subsystem_, count * sizeof(T));
        return result;
    }
    void deallocate(T* pointer, std::size_t count) noexcept {
        if (subsystem_ != MemoryTracker::invalid_subsystem)
            MemoryTracker::instance().note_free(subsystem_, count * sizeof(T));
        std::allocator<T>{}.deallocate(pointer, count);
    }

    template <class U>
    struct rebind { using other = TrackedAllocator<U>; };

    MemoryTracker::SubsystemId subsystem_{};

    template <class U>
    friend bool operator==(const TrackedAllocator& a, const TrackedAllocator<U>& b) noexcept {
        return a.subsystem_ == b.subsystem_;
    }
};

} // namespace stellar::engine
