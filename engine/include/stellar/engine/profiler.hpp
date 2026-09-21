#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

struct ProfileSpan {
    std::string name;
    std::string category;
    std::uint64_t thread_id{};
    std::uint64_t start_nanoseconds{};
    std::uint64_t duration_nanoseconds{};
};

struct ProfileFrame {
    std::uint64_t index{};
    std::uint64_t wall_nanoseconds{};
    std::vector<ProfileSpan> spans;
    std::unordered_map<std::string, std::int64_t> counters;
    std::unordered_map<std::string, double> gauges;
};

struct ProfileAggregate {
    std::string name;
    std::string category;
    std::uint64_t calls{};
    std::uint64_t total_nanoseconds{};
    std::uint64_t max_nanoseconds{};
};

// Process-wide CPU profiler. Span recording is per-thread and cheap (a mutex
// is only touched when a thread-local buffer flushes at frame end); counters
// and gauges are aggregated under the same lock. GPU pass timings are left to
// the render layer, which records them as ordinary spans.
class Profiler {
public:
    static Profiler& instance();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    class Scope {
    public:
        Scope() = default;
        ~Scope();
        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&& other) noexcept;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        friend class Profiler;
        Scope(std::string_view name, std::string_view category,
              std::chrono::steady_clock::time_point start)
            : name_(name), category_(category), start_(start), active_(true) {}
        std::string name_;
        std::string category_;
        std::chrono::steady_clock::time_point start_{};
        bool active_{};
    };

    [[nodiscard]] Scope span(std::string_view name, std::string_view category = {});
    void record_span(ProfileSpan span);
    void add_counter(std::string_view name, std::int64_t delta);
    void set_gauge(std::string_view name, double value);

    void begin_frame();
    [[nodiscard]] ProfileFrame end_frame();
    [[nodiscard]] std::deque<ProfileFrame> recent_frames() const;
    [[nodiscard]] std::size_t retained_frames() const noexcept { return retained_frames_; }
    void set_retained_frames(std::size_t frames) noexcept { retained_frames_ = frames; }

    [[nodiscard]] std::vector<ProfileAggregate> aggregates() const;
    void reset_aggregates();

    [[nodiscard]] std::string export_json() const;
    // Short human-readable rows for the developer overlay.
    [[nodiscard]] std::vector<std::string> overlay_lines(std::size_t max_rows = 12) const;

    struct ThreadSpans;

private:
    Profiler() = default;
    struct AggregateKey {
        std::string name;
        std::string category;
        bool operator==(const AggregateKey&) const = default;
    };
    struct AggregateHash {
        std::size_t operator()(const AggregateKey& key) const noexcept;
    };
    struct AggregateState {
        std::uint64_t calls{};
        std::uint64_t total_nanoseconds{};
        std::uint64_t max_nanoseconds{};
    };

    void register_thread_buffer(ThreadSpans* buffer);
    void unregister_thread_buffer(ThreadSpans* buffer);
    void register_once(ThreadSpans& storage);
    void drain_thread_buffers_locked();
    static std::uint64_t thread_key();

    bool enabled_{true};
    mutable std::mutex mutex_;
    std::deque<ProfileFrame> frames_;
    std::size_t retained_frames_{240};
    std::unordered_map<AggregateKey, AggregateState, AggregateHash> aggregates_;
    std::unordered_map<std::string, std::int64_t> frame_counters_;
    std::unordered_map<std::string, double> frame_gauges_;
    std::vector<ProfileSpan> pending_spans_;
    std::vector<ThreadSpans*> thread_buffers_;
    std::chrono::steady_clock::time_point frame_start_{};
    std::uint64_t next_frame_index_{};
    bool frame_open_{};
};

} // namespace stellar::engine

#define STELLAR_PROFILE_SCOPE(name) \
    auto stellar_profile_scope_##__LINE__ = ::stellar::engine::Profiler::instance().span(name)

#define STELLAR_PROFILE_SCOPE_CAT(name, category) \
    auto stellar_profile_scope_##__LINE__ = ::stellar::engine::Profiler::instance().span(name, category)
