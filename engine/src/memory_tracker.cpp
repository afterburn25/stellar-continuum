#include <stellar/engine/memory_tracker.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace stellar::engine {

MemoryTracker& MemoryTracker::instance() {
    static MemoryTracker* tracker = new MemoryTracker();
    return *tracker;
}

MemoryTracker::SubsystemId MemoryTracker::register_subsystem(std::string_view name) {
    std::lock_guard lock(mutex_);
    const auto existing = by_name_.find(std::string{name});
    if (existing != by_name_.end()) return existing->second;
    if (subsystems_.size() >= invalid_subsystem)
        throw std::overflow_error("MemoryTracker subsystem registry exhausted");
    const SubsystemId id = static_cast<SubsystemId>(subsystems_.size());
    subsystems_.push_back(SubsystemMemoryStats{.name = std::string{name}});
    by_name_.emplace(subsystems_.back().name, id);
    return id;
}

void MemoryTracker::note_alloc(SubsystemId subsystem, std::uint64_t bytes) {
    std::lock_guard lock(mutex_);
    if (subsystem >= subsystems_.size()) return;
    auto& entry = subsystems_[subsystem];
    entry.current_bytes += bytes;
    entry.high_water_bytes = std::max(entry.high_water_bytes, entry.current_bytes);
    ++entry.allocation_count;
}

void MemoryTracker::note_free(SubsystemId subsystem, std::uint64_t bytes) {
    std::lock_guard lock(mutex_);
    if (subsystem >= subsystems_.size()) return;
    auto& entry = subsystems_[subsystem];
    entry.current_bytes = bytes > entry.current_bytes ? 0 : entry.current_bytes - bytes;
    ++entry.deallocation_count;
}

void MemoryTracker::report(SubsystemId subsystem, std::uint64_t used_bytes,
                           std::uint64_t reserved_bytes) {
    std::lock_guard lock(mutex_);
    if (subsystem >= subsystems_.size()) return;
    auto& entry = subsystems_[subsystem];
    entry.current_bytes = used_bytes;
    entry.reserved_bytes = reserved_bytes;
    entry.high_water_bytes = std::max(entry.high_water_bytes, used_bytes);
}

MemorySnapshot MemoryTracker::snapshot() const {
    std::lock_guard lock(mutex_);
    MemorySnapshot snapshot;
    snapshot.subsystems = subsystems_;
    for (const auto& entry : subsystems_) {
        snapshot.total_current_bytes += entry.current_bytes;
        snapshot.total_high_water_bytes += entry.high_water_bytes;
    }
    return snapshot;
}

std::string MemoryTracker::export_json() const {
    const auto snapshot = this->snapshot();
    std::ostringstream out;
    out << "{\"totalCurrentBytes\":" << snapshot.total_current_bytes
        << ",\"totalHighWaterBytes\":" << snapshot.total_high_water_bytes
        << ",\"subsystems\":[";
    bool first = true;
    for (const auto& entry : snapshot.subsystems) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":\"" << entry.name
            << "\",\"currentBytes\":" << entry.current_bytes
            << ",\"reservedBytes\":" << entry.reserved_bytes
            << ",\"highWaterBytes\":" << entry.high_water_bytes
            << ",\"allocations\":" << entry.allocation_count
            << ",\"deallocations\":" << entry.deallocation_count << '}';
    }
    out << "]}";
    return out.str();
}

std::vector<std::string> MemoryTracker::overlay_lines(std::size_t max_rows) const {
    const auto snapshot = this->snapshot();
    std::vector<SubsystemMemoryStats> sorted = snapshot.subsystems;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.current_bytes > b.current_bytes;
    });
    std::vector<std::string> lines;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "tracked %.1f MiB current / %.1f MiB high-water",
                  static_cast<double>(snapshot.total_current_bytes) / (1024.0 * 1024.0),
                  static_cast<double>(snapshot.total_high_water_bytes) / (1024.0 * 1024.0));
    lines.emplace_back(buf);
    for (const auto& entry : sorted) {
        if (lines.size() >= max_rows + 1) break;
        std::snprintf(buf, sizeof(buf), "%s  %.1f MiB  (peak %.1f MiB, %llu allocs)",
                      entry.name.c_str(),
                      static_cast<double>(entry.current_bytes) / (1024.0 * 1024.0),
                      static_cast<double>(entry.high_water_bytes) / (1024.0 * 1024.0),
                      static_cast<unsigned long long>(entry.allocation_count));
        lines.emplace_back(buf);
    }
    return lines;
}

void MemoryTracker::reset_high_water_marks() {
    std::lock_guard lock(mutex_);
    for (auto& entry : subsystems_) entry.high_water_bytes = entry.current_bytes;
}

} // namespace stellar::engine
