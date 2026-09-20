#include <stellar/engine/event_bus.hpp>

#include <algorithm>

namespace stellar::engine {

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        unsubscribe();
        bus_ = other.bus_;
        type_ = other.type_;
        id_ = other.id_;
        other.bus_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

void Subscription::unsubscribe() {
    if (!bus_) return;
    bus_->remove_subscription(type_, id_);
    bus_ = nullptr;
    id_ = 0;
}

bool Subscription::active() const noexcept { return bus_ != nullptr; }

void EventBus::dispatch(std::type_index type, const void* payload) {
    auto it = handlers_.find(type);
    if (it == handlers_.end()) return;
    auto& list = it->second;
    ++list.dispatching;
    // Entries may be marked dead mid-dispatch; they are swept afterward.
    for (auto& entry : list.entries) {
        if (!entry.alive) continue;
        entry.call(payload);
        ++stats_.dispatched;
    }
    --list.dispatching;
    if (list.dispatching == 0)
        std::erase_if(list.entries, [](const HandlerEntry& entry) { return !entry.alive; });
}

void EventBus::remove_subscription(std::type_index type, std::uint64_t id) {
    require_owner();
    auto it = handlers_.find(type);
    if (it == handlers_.end()) return;
    auto& list = it->second;
    for (auto& entry : list.entries) {
        if (entry.id == id) {
            entry.alive = false;
            if (list.dispatching == 0)
                std::erase_if(list.entries, [](const HandlerEntry& entry) { return !entry.alive; });
            return;
        }
    }
}

std::size_t EventBus::drain(Tick tick) {
    require_owner();
    std::vector<Deferred> due;
    auto it = deferred_.begin();
    while (it != deferred_.end()) {
        if (it->tick <= tick) {
            due.push_back(std::move(*it));
            it = deferred_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(due.begin(), due.end(), [](const Deferred& a, const Deferred& b) {
        return a.sequence < b.sequence;
    });
    for (const auto& deferred : due) dispatch(deferred.type, deferred.holder->payload());
    stats_.pending = deferred_.size();
    return due.size();
}

void EventBus::clear_deferred() {
    require_owner();
    deferred_.clear();
    stats_.pending = 0;
}

EventBusStats EventBus::stats() const {
    EventBusStats snapshot = stats_;
    snapshot.pending = deferred_.size();
    std::size_t subscriptions = 0;
    for (const auto& [type, list] : handlers_)
        for (const auto& entry : list.entries) if (entry.alive) ++subscriptions;
    snapshot.subscriptions = subscriptions;
    return snapshot;
}

} // namespace stellar::engine
