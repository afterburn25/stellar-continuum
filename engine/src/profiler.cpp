#include <stellar/engine/profiler.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <span>
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

std::string serialize_capture(std::span<const ProfileFrame> frames,
                              std::span<const ProfileAggregate> aggregates) {
    std::ostringstream out;
    out << "{\"frames\":[";
    bool first_frame = true;
    for (const auto& frame : frames) {
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
    for (const auto& aggregate : aggregates) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":\"" << json_escape(aggregate.name)
            << "\",\"category\":\"" << json_escape(aggregate.category)
            << "\",\"calls\":" << aggregate.calls
            << ",\"totalNs\":" << aggregate.total_nanoseconds
            << ",\"maxNs\":" << aggregate.max_nanoseconds << '}';
    }
    out << "]}";
    return out.str();
}

} // namespace

// Per-thread span storage registered with the profiler so the frame owner can
// drain every thread's spans, not just its own. Lock order is always
// buffer->mutex then Profiler::mutex_.
struct Profiler::ThreadSpans {
    ~ThreadSpans();
    std::mutex mutex;
    std::vector<ProfileSpan> spans;
    std::unordered_map<AggregateKey, AggregateState, AggregateHash> aggregates;
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
    merge_thread_aggregates_locked(*buffer);
}

void Profiler::merge_thread_aggregates_locked(ThreadSpans& buffer) {
    for (const auto& [key, state] : buffer.aggregates) {
        auto& aggregate = aggregates_[key];
        aggregate.calls += state.calls;
        aggregate.total_nanoseconds += state.total_nanoseconds;
        aggregate.max_nanoseconds = std::max(aggregate.max_nanoseconds, state.max_nanoseconds);
    }
    buffer.aggregates.clear();
}

void Profiler::drain_thread_buffers_locked() {
    for (auto* buffer : thread_buffers_) {
        std::lock_guard buffer_lock(buffer->mutex);
        for (auto& span : buffer->spans) pending_spans_.push_back(std::move(span));
        buffer->spans.clear();
        merge_thread_aggregates_locked(*buffer);
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
    auto& storage = thread_spans();
    // First use on this thread registers the buffer so frame boundaries can
    // drain it; register before pushing so the drain cannot miss this span.
    register_once(storage);
    std::lock_guard buffer_lock(storage.mutex);
    auto& aggregate = storage.aggregates[{span.name, span.category}];
    ++aggregate.calls;
    aggregate.total_nanoseconds += span.duration_nanoseconds;
    aggregate.max_nanoseconds = std::max(aggregate.max_nanoseconds, span.duration_nanoseconds);
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

std::vector<ProfileAggregate> Profiler::aggregates_merged_locked() const {
    // Snapshot the shared aggregates, then merge per-thread buffers that
    // have not drained since the last frame boundary so readers keep the
    // real-time contract the recording path had before aggregation moved
    // off the global lock.
    std::unordered_map<AggregateKey, AggregateState, AggregateHash> merged = aggregates_;
    for (auto* buffer : thread_buffers_) {
        std::lock_guard buffer_lock(buffer->mutex);
        for (const auto& [key, state] : buffer->aggregates) {
            auto& aggregate = merged[key];
            aggregate.calls += state.calls;
            aggregate.total_nanoseconds += state.total_nanoseconds;
            aggregate.max_nanoseconds = std::max(aggregate.max_nanoseconds, state.max_nanoseconds);
        }
    }
    std::vector<ProfileAggregate> out;
    out.reserve(merged.size());
    for (const auto& [key, state] : merged) {
        out.push_back({key.name, key.category, state.calls,
                       state.total_nanoseconds, state.max_nanoseconds});
    }
    return out;
}

std::vector<ProfileAggregate> Profiler::aggregates() const {
    std::lock_guard lock(mutex_);
    auto out = aggregates_merged_locked();
    std::sort(out.begin(), out.end(), [](const ProfileAggregate& a, const ProfileAggregate& b) {
        return a.total_nanoseconds > b.total_nanoseconds;
    });
    return out;
}

void Profiler::reset_aggregates() {
    std::lock_guard lock(mutex_);
    // Drain first so per-thread aggregates accumulated before the reset
    // cannot resurface afterwards.
    drain_thread_buffers_locked();
    aggregates_.clear();
}

std::string Profiler::export_json() const {
    std::lock_guard lock(mutex_);
    const std::vector<ProfileFrame> frames(frames_.begin(), frames_.end());
    const auto aggregate_list = aggregates_merged_locked();
    return serialize_capture(
        std::span<const ProfileFrame>(frames),
        std::span<const ProfileAggregate>(aggregate_list));
}

std::optional<ProfileCapture>
ProfileCapture::parse(std::string_view json_document) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_document);
    } catch (...) {
        return std::nullopt;
    }
    if (!doc.is_object() || !doc.contains("frames") ||
        !doc["frames"].is_array() || !doc.contains("aggregates") ||
        !doc["aggregates"].is_array())
        return std::nullopt;
    ProfileCapture capture;
    for (const auto& frame_json : doc["frames"]) {
        if (!frame_json.is_object()) return std::nullopt;
        ProfileFrame frame;
        frame.index = frame_json.value("index", std::uint64_t{});
        frame.wall_nanoseconds =
            frame_json.value("wallNs", std::uint64_t{});
        for (const auto& span_json :
             frame_json.value("spans", nlohmann::json::array())) {
            ProfileSpan span;
            span.name = span_json.value("name", std::string{});
            span.category = span_json.value("category", std::string{});
            span.thread_id = span_json.value("thread", std::uint64_t{});
            span.start_nanoseconds =
                span_json.value("startNs", std::uint64_t{});
            span.duration_nanoseconds =
                span_json.value("durationNs", std::uint64_t{});
            frame.spans.push_back(std::move(span));
        }
        for (const auto& [name, value] :
             frame_json.value("counters", nlohmann::json::object()).items())
            if (value.is_number_integer())
                frame.counters[name] = value.get<std::int64_t>();
        capture.frames.push_back(std::move(frame));
    }
    for (const auto& aggregate_json : doc["aggregates"]) {
        if (!aggregate_json.is_object()) return std::nullopt;
        ProfileAggregate aggregate;
        aggregate.name = aggregate_json.value("name", std::string{});
        aggregate.category =
            aggregate_json.value("category", std::string{});
        aggregate.calls = aggregate_json.value("calls", std::uint64_t{});
        aggregate.total_nanoseconds =
            aggregate_json.value("totalNs", std::uint64_t{});
        aggregate.max_nanoseconds =
            aggregate_json.value("maxNs", std::uint64_t{});
        capture.aggregates.push_back(std::move(aggregate));
    }
    return capture;
}

std::string ProfileCapture::to_json() const {
    return serialize_capture(
        std::span<const ProfileFrame>(frames),
        std::span<const ProfileAggregate>(aggregates));
}

namespace {
struct ComparisonKeyHash {
    std::size_t
    operator()(const std::pair<std::string, std::string> &key) const noexcept {
        return std::hash<std::string>{}(key.first + '\x1f' + key.second);
    }
};
} // namespace

std::vector<ProfileComparisonRow>
compare_captures(const ProfileCapture &a, const ProfileCapture &b) {
    std::unordered_map<std::pair<std::string, std::string>,
                       ProfileComparisonRow, ComparisonKeyHash>
        rows;
    const auto add = [&rows](const ProfileAggregate &aggregate, bool second) {
        auto &row = rows[{aggregate.name, aggregate.category}];
        row.name = aggregate.name;
        row.category = aggregate.category;
        const double mean =
            aggregate.calls
                ? static_cast<double>(aggregate.total_nanoseconds) /
                      static_cast<double>(aggregate.calls)
                : 0.0;
        if (second) {
            row.calls_b = aggregate.calls;
            row.mean_ns_b = mean;
        } else {
            row.calls_a = aggregate.calls;
            row.mean_ns_a = mean;
        }
    };
    for (const auto &aggregate : a.aggregates) add(aggregate, false);
    for (const auto &aggregate : b.aggregates) add(aggregate, true);
    std::vector<ProfileComparisonRow> out;
    out.reserve(rows.size());
    for (auto &[key, row] : rows) {
        (void)key;
        out.push_back(std::move(row));
    }
    std::sort(out.begin(), out.end(), [](const auto &lhs, const auto &rhs) {
        return std::fabs(lhs.mean_ns_b - lhs.mean_ns_a) >
               std::fabs(rhs.mean_ns_b - rhs.mean_ns_a);
    });
    return out;
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
