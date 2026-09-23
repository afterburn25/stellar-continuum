#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <stellar/engine/foundation.hpp>

namespace stellar::engine {

class EventBus;

// RAII subscription handle. Dropping the handle unsubscribes; unsubscribing
// while an event is being dispatched takes effect after the current dispatch.
class Subscription {
public:
    Subscription() = default;
    ~Subscription() { unsubscribe(); }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept { *this = std::move(other); }
    Subscription& operator=(Subscription&& other) noexcept;
    void unsubscribe();
    [[nodiscard]] bool active() const noexcept;
private:
    friend class EventBus;
    Subscription(EventBus* bus, std::type_index type, std::uint64_t id)
        : bus_(bus), type_(type), id_(id) {}
    EventBus* bus_{};
    std::type_index type_{typeid(void)};
    std::uint64_t id_{};
};

struct EventBusStats {
    std::uint64_t published{};
    std::uint64_t dispatched{};
    std::uint64_t deferred{};
    std::uint64_t pending{};
    std::size_t subscriptions{};
};

// Typed synchronous event bus with an owner thread (the simulation owner) and
// a deferred queue ordered by (tick, sequence). publish() dispatches to
// subscribers immediately in registration order; defer() queues for a later
// drain() so gameplay can batch events to a tick boundary. Domain event types
// live in Core; the bus carries any copyable/movable payload.
class EventBus {
public:
    EventBus() : owner_(std::this_thread::get_id()) {}
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template <class T>
    [[nodiscard]] Subscription subscribe(std::function<void(const T&)> handler) {
        require_owner();
        if (!handler) throw std::invalid_argument("EventBus cannot subscribe an empty handler");
        auto& handlers = handlers_[std::type_index(typeid(T))];
        const std::uint64_t id = next_subscription_++;
        handlers.entries.push_back(HandlerEntry{
            id, true,
            [handler = std::move(handler)](const void* payload) {
                handler(*static_cast<const T*>(payload));
            }});
        return Subscription{this, std::type_index(typeid(T)), id};
    }

    template <class T>
    void publish(const T& event) {
        require_owner();
        ++stats_.published;
        dispatch(std::type_index(typeid(T)), &event);
    }

    template <class T>
    void defer(Tick tick, T event) {
        require_owner();
        auto holder = std::make_shared<DeferredHolder<T>>(std::move(event));
        deferred_.push_back(Deferred{tick, next_sequence_++,
                                     std::type_index(typeid(T)), std::move(holder)});
        ++stats_.deferred;
    }

    // Dispatches every queued event with tick <= the given tick, in publish
    // order. Returns the number dispatched.
    std::size_t drain(Tick tick);
    // Discards queued events without dispatching.
    void clear_deferred();
    [[nodiscard]] EventBusStats stats() const;

private:
    struct DeferredHolderBase {
        virtual ~DeferredHolderBase() = default;
        virtual const void* payload() const = 0;
    };
    template <class T>
    struct DeferredHolder : DeferredHolderBase {
        explicit DeferredHolder(T value) : value(std::move(value)) {}
        const void* payload() const override { return &value; }
        T value;
    };
    struct Deferred {
        Tick tick{};
        std::uint64_t sequence{};
        std::type_index type;
        std::shared_ptr<DeferredHolderBase> holder;
    };
    struct HandlerEntry {
        std::uint64_t id{};
        bool alive{true};
        std::function<void(const void*)> call;
    };
    struct HandlerList {
        std::vector<HandlerEntry> entries;
        std::uint32_t dispatching{};
    };

    void dispatch(std::type_index type, const void* payload);
    void remove_subscription(std::type_index type, std::uint64_t id);
    void require_owner() const {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error("EventBus may only be used by its owner thread");
    }
    friend class Subscription;

    std::thread::id owner_;
    std::unordered_map<std::type_index, HandlerList> handlers_;
    std::deque<Deferred> deferred_;
    std::uint64_t next_subscription_{1};
    std::uint64_t next_sequence_{};
    EventBusStats stats_{};
};

} // namespace stellar::engine
