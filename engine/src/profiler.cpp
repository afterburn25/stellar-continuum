#include <stellar/engine/profiler.hpp>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <sstream>
#include <thread>

namespace stellar::engine {

namespace {

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

} // namespace

// Per-thread span storage registered with the profiler so the frame owner can
// drain every thread's spans, not just its own. Lock order is always
// buffer->mutex then Profiler::mutex_.
struct Profiler::ThreadSpans {
    ~ThreadSpans();
    std::mutex mutex;
    std::vector<ProfileSpan> spans;
};

namespace {

Profiler::ThreadSpans& thread_spans() {
    static thread_local Profiler::ThreadSpans storage;
    return storage;
}

} // namespace

Profiler& Profiler::instance() {
    // Deliberately leaked: worker threads may outlive static destruction and
    // still drain into the registry during thread teardown.
    static Profiler* profiler = new Profiler();
    return *profiler;
}

Profiler::ThreadSpans::~ThreadSpans() {
    Profiler::instance().unregister_thread_buffer(this);
}

void Profiler::register_thread_buffer(ThreadSpans* buffer) {
    std::lock_guard lock(mutex_);
    thread_buffers_.push_back(buffer);
}

void Profiler::unregister_thread_buffer(ThreadSpans* buffer) {
    std::lock_guard lock(mutex_);
    std::erase(thread_buffers_, buffer);
    std::lock_guard buffer_lock(buffer->mutex);
    for (auto& span : buffer->spans) pending_spans_.push_back(std::move(span));
    buffer->spans.clear();
}

void Profiler::drain_thread_buffers_locked() {
    for (auto* buffer : thread_buffers_) {
        std::lock_guard buffer_lock(buffer->mutex);
        for (auto& span : buffer->spans) pending_spans_.push_back(std::move(span));
        buffer->spans.clear();
    }
}

std::size_t Profiler::AggregateHash::operator()(const AggregateKey& key) const noexcept {
    return std::hash<std::string>{}(key.name) ^ (std::hash<std::string>{}(key.category) << 1);
}

std::uint64_t Profiler::thread_key() {
    return static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

Profiler::Scope::~Scope() {
    if (!active_) return;
    const auto end = std::chrono::steady_clock::now();
    ProfileSpan span;
    span.name = std::move(name_);
    span.category = std::move(category_);
    span.thread_id = thread_key();
    span.start_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            start_.time_since_epoch()).count());
    span.duration_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count());
    Profiler::instance().record_span(std::move(span));
}

Profiler::Scope::Scope(Scope&& other) noexcept
    : name_(std::move(other.name_)), category_(std::move(other.category_)),
      start_(other.start_), active_(other.active_) {
    other.active_ = false;
}

Profiler::Scope& Profiler::Scope::operator=(Scope&& other) noexcept {
    if (this != &other) {
        name_ = std::move(other.name_);
        category_ = std::move(other.category_);
        start_ = other.start_;
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

Profiler::Scope Profiler::span(std::string_view name, std::string_view category) {
    if (!enabled_) return Scope{};
    return Scope{name, category, std::chrono::steady_clock::now()};
}

void Profiler::record_span(ProfileSpan span) {
    if (!enabled_) return;
    {
        std::lock_guard lock(mutex_);
        auto& aggregate = aggregates_[{span.name, span.category}];
        ++aggregate.calls;
        aggregate.total_nanoseconds += span.duration_nanoseconds;
        aggregate.max_nanoseconds = std::max(aggregate.max_nanoseconds, span.duration_nanoseconds);
    }
    auto& storage = thread_spans();
    // First use on this thread registers the buffer so frame boundaries can
    // drain it; register before pushing so the drain cannot miss this span.
    register_once(storage);
    std::lock_guard buffer_lock(storage.mutex);
    storage.spans.push_back(std::move(span));
}

void Profiler::register_once(ThreadSpans& storage) {
    static thread_local bool registered = false;
    if (!registered) {
        registered = true;
        register_thread_buffer(&storage);
    }
}

void Profiler::add_counter(std::string_view name, std::int64_t delta) {
    std::lock_guard lock(mutex_);
    frame_counters_[std::string{name}] += delta;
}

void Profiler::set_gauge(std::string_view name, double value) {
    std::lock_guard lock(mutex_);
    frame_gauges_[std::string{name}] = value;
}

void Profiler::begin_frame() {
    std::lock_guard lock(mutex_);
    drain_thread_buffers_locked();
    frame_start_ = std::chrono::steady_clock::now();
    frame_open_ = true;
    frame_counters_.clear();
    frame_gauges_.clear();
}

ProfileFrame Profiler::end_frame() {
    std::lock_guard lock(mutex_);
    ProfileFrame frame;
    if (!frame_open_) return frame;
    drain_thread_buffers_locked();
    frame.index = next_frame_index_++;
    frame.wall_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - frame_start_).count());
    frame.counters = std::move(frame_counters_);
    frame.gauges = std::move(frame_gauges_);
    frame_counters_.clear();
    frame_gauges_.clear();
    frame.spans = std::move(pending_spans_);
    pending_spans_.clear();
    frame_open_ = false;
    frames_.push_back(std::move(frame));
    while (frames_.size() > retained_frames_) frames_.pop_front();
    return frames_.empty() ? ProfileFrame{} : frames_.back();
}

std::deque<ProfileFrame> Profiler::recent_frames() const {
    std::lock_guard lock(mutex_);
    return frames_;
}

std::vector<ProfileAggregate> Profiler::aggregates() const {
    std::lock_guard lock(mutex_);
    std::vector<ProfileAggregate> out;
    out.reserve(aggregates_.size());
    for (const auto& [key, state] : aggregates_) {
        out.push_back({key.name, key.category, state.calls,
                       state.total_nanoseconds, state.max_nanoseconds});
    }
    std::sort(out.begin(), out.end(), [](const ProfileAggregate& a, const ProfileAggregate& b) {
        return a.total_nanoseconds > b.total_nanoseconds;
    });
    return out;
}

void Profiler::reset_aggregates() {
    std::lock_guard lock(mutex_);
    aggregates_.clear();
}

std::string Profiler::export_json() const {
    std::lock_guard lock(mutex_);
    std::ostringstream out;
    out << "{\"frames\":[";
    bool first_frame = true;
    for (const auto& frame : frames_) {
        if (!first_frame) out << ',';
        first_frame = false;
        out << "{\"index\":" << frame.index
            << ",\"wallNs\":" << frame.wall_nanoseconds << ",\"spans\":[";
        bool first_span = true;
        for (const auto& span : frame.spans) {
            if (!first_span) out << ',';
            first_span = false;
            out << "{\"name\":\"" << json_escape(span.name)
                << "\",\"category\":\"" << json_escape(span.category)
                << "\",\"thread\":" << span.thread_id
                << ",\"startNs\":" << span.start_nanoseconds
                << ",\"durationNs\":" << span.duration_nanoseconds << '}';
        }
        out << "],\"counters\":{";
        bool first_counter = true;
        for (const auto& [name, value] : frame.counters) {
            if (!first_counter) out << ',';
            first_counter = false;
            out << '\"' << json_escape(name) << "\":" << value;
        }
        out << "}}";
    }
    out << "],\"aggregates\":[";
    bool first = true;
    for (const auto& [key, state] : aggregates_) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":\"" << json_escape(key.name)
            << "\",\"category\":\"" << json_escape(key.category)
            << "\",\"calls\":" << state.calls
            << ",\"totalNs\":" << state.total_nanoseconds
            << ",\"maxNs\":" << state.max_nanoseconds << '}';
    }
    out << "]}";
    return out.str();
}

std::vector<std::string> Profiler::overlay_lines(std::size_t max_rows) const {
    std::vector<std::string> lines;
    const auto frames = recent_frames();
    if (!frames.empty()) {
        std::uint64_t total = 0;
        for (const auto& frame : frames) total += frame.wall_nanoseconds;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "frame mean %.2f ms over %zu retained",
                      static_cast<double>(total) / (1e6 * frames.size()), frames.size());
        lines.emplace_back(buf);
    }
    const auto aggregate_list = aggregates();
    for (const auto& aggregate : aggregate_list) {
        if (lines.size() >= max_rows + 1) break;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s  calls=%llu  mean=%.2f ms  max=%.2f ms",
                      aggregate.name.c_str(),
                      static_cast<unsigned long long>(aggregate.calls),
                      aggregate.calls ? static_cast<double>(aggregate.total_nanoseconds) /
                                            (1e6 * aggregate.calls)
                                    : 0.0,
                      static_cast<double>(aggregate.max_nanoseconds) / 1e6);
        lines.emplace_back(buf);
    }
    return lines;
}

} // namespace stellar::engine
