#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stellar/engine/foundation.hpp>

namespace stellar::engine {

// Unified entity/world container: generational entity IDs (EntityRegistry),
// type-erased sparse-set component stores, parent/child hierarchy with
// acyclicity enforcement, and explicit binary snapshot/restore with
// registered component codecs. Dense-array offsets and scene-node addresses
// are never durable identity — EntityId is.
class World {
public:
    enum class DestroyMode { DetachChildren, DestroyDescendants };

    // --- lifecycle ---------------------------------------------------------
    [[nodiscard]] EntityId create();
    // Returns false for stale/unknown IDs. DetachChildren clears children's
    // parent links; DestroyDescendants removes the whole subtree.
    bool destroy(EntityId id, DestroyMode mode = DestroyMode::DetachChildren);
    [[nodiscard]] bool alive(EntityId id) const noexcept { return registry_.contains(id); }
    [[nodiscard]] std::size_t size() const noexcept { return registry_.size(); }
    [[nodiscard]] std::vector<EntityId> entities() const;

    // --- hierarchy ---------------------------------------------------------
    // Throws std::invalid_argument on self-parenting or a cycle. Both entities
    // must be alive.
    void set_parent(EntityId child, EntityId parent);
    void clear_parent(EntityId child);
    [[nodiscard]] std::optional<EntityId> parent(EntityId id) const;
    [[nodiscard]] std::vector<EntityId> children(EntityId id) const;
    [[nodiscard]] EntityId root(EntityId id) const;

    // --- components --------------------------------------------------------
    template <class T>
    T& add(EntityId id, T component) {
        return store<T>().add(id, std::move(component));
    }
    template <class T>
    [[nodiscard]] T* get(EntityId id) {
        return alive(id) ? store<T>().get(id) : nullptr;
    }
    template <class T>
    [[nodiscard]] const T* get(EntityId id) const {
        return alive(id) ? store<T>().get(id) : nullptr;
    }
    template <class T>
    [[nodiscard]] bool has(EntityId id) const {
        return alive(id) && store<T>().has(id);
    }
    template <class T>
    bool remove(EntityId id) {
        return alive(id) && store<T>().remove(id);
    }
    // Every alive entity carrying T, as (id, component) pairs.
    template <class T>
    [[nodiscard]] std::vector<std::pair<EntityId, T*>> view() {
        std::vector<std::pair<EntityId, T*>> out;
        auto& s = store<T>();
        for (EntityId id : s.entities()) if (alive(id)) out.emplace_back(id, s.get(id));
        return out;
    }
    // Alive entities carrying every listed component type.
    template <class First, class... Rest>
    [[nodiscard]] std::vector<EntityId> query() {
        std::vector<EntityId> out;
        for (auto& [id, component] : view<First>())
            if ((has<Rest>(id) && ...)) out.push_back(id);
        return out;
    }

    // --- legacy identity mapping -------------------------------------------
    // Explicit bridge for game subsystems that still use integer IDs; never
    // reinterpret legacy integers as EntityId fields.
    void bind_legacy(EntityId id, std::int64_t legacy_id);
    [[nodiscard]] std::optional<EntityId> entity_for_legacy(std::int64_t legacy_id) const;
    [[nodiscard]] std::optional<std::int64_t> legacy_for(EntityId id) const;

    // --- serialization -------------------------------------------------------
    // Register a component codec before snapshot/restore. Names must be unique
    // and stable across builds — they are the persisted identity of a type.
    template <class T>
    void register_component(std::string name,
                            std::function<std::vector<std::uint8_t>(const T&)> encode,
                            std::function<T(const std::vector<std::uint8_t>&)> decode) {
        Codec codec{std::move(name), std::type_index(typeid(T)),
                    [this](EntityId id) -> const void* { return store<T>().get(id); },
                    [encode](const void* component) {
                        return encode(*static_cast<const T*>(component));
                    },
                    [this, decode](EntityId id, const std::vector<std::uint8_t>& bytes) {
                        add<T>(id, decode(bytes));
                    }};
        codecs_.emplace(codec.name, std::move(codec));
    }

    // Binary snapshot: magic + version + checksum, entity records (id,
    // components by registered name), hierarchy edges, legacy bindings.
    [[nodiscard]] std::vector<std::uint8_t> snapshot() const;
    // One hash per registered component name present in the world,
    // folded over each entity's canonical encoded bytes in entity order.
    // A diverging snapshot can thus be localized to the component type
    // instead of "the world differs" — replay checkpoints use these as
    // labeled sections.
    [[nodiscard]] std::vector<std::pair<std::string, std::uint64_t>>
    component_hashes() const;
    // Restores entities/components/hierarchy into this (cleared) world.
    // Throws on magic/version/checksum/codec mismatch.
    void restore(const std::vector<std::uint8_t>& bytes);
    void clear();

    // Approximate container-storage footprint: vector capacities plus
    // node/bucket estimates for the index maps. Intended for
    // MemoryTracker::report — measures occupancy, not allocator truth.
    [[nodiscard]] std::size_t estimated_memory_bytes() const;

    static constexpr std::uint32_t snapshot_magic = 0x31575453; // "STW1"
    static constexpr std::uint32_t snapshot_version = 1;

private:
    struct Codec {
        std::string name;
        std::type_index type;
        std::function<const void*(EntityId)> fetch;
        std::function<std::vector<std::uint8_t>(const void*)> encode;
        std::function<void(EntityId, const std::vector<std::uint8_t>&)> decode_into;
    };

    class ComponentStoreBase {
    public:
        virtual ~ComponentStoreBase() = default;
        virtual void erase(EntityId id) = 0;
        [[nodiscard]] virtual std::size_t memory_bytes() const noexcept = 0;
    };

    template <class T>
    class ComponentStore final : public ComponentStoreBase {
    public:
        T& add(EntityId id, T component) {
            if (id.index >= sparse_.size()) sparse_.resize(id.index + 1, sentinel);
            if (sparse_[id.index] != sentinel) {
                dense_[sparse_[id.index]] = std::move(component);
                return dense_[sparse_[id.index]];
            }
            sparse_[id.index] = static_cast<std::uint32_t>(dense_.size());
            dense_ids_.push_back(id);
            dense_.push_back(std::move(component));
            return dense_.back();
        }
        T* get(EntityId id) {
            if (id.index >= sparse_.size() || sparse_[id.index] == sentinel) return nullptr;
            if (dense_ids_[sparse_[id.index]] != id) return nullptr; // stale generation
            return &dense_[sparse_[id.index]];
        }
        const T* get(EntityId id) const {
            return const_cast<ComponentStore*>(this)->get(id);
        }
        bool has(EntityId id) const { return get(id) != nullptr; }
        bool remove(EntityId id) {
            if (!has(id)) return false;
            const std::uint32_t dense_index = sparse_[id.index];
            const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1);
            if (dense_index != last) {
                dense_[dense_index] = std::move(dense_[last]);
                dense_ids_[dense_index] = dense_ids_[last];
                sparse_[dense_ids_[dense_index].index] = dense_index;
            }
            dense_.pop_back();
            dense_ids_.pop_back();
            sparse_[id.index] = sentinel;
            return true;
        }
        void erase(EntityId id) override { remove(id); }
        [[nodiscard]] const std::vector<EntityId>& entities() const { return dense_ids_; }
        std::size_t memory_bytes() const noexcept override {
            return sparse_.capacity()*sizeof(std::uint32_t)
                 + dense_ids_.capacity()*sizeof(EntityId)
                 + dense_.capacity()*sizeof(T);
        }

        static constexpr std::uint32_t sentinel = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> sparse_;
        std::vector<EntityId> dense_ids_;
        std::vector<T> dense_;
    };

    template <class T>
    ComponentStore<T>& store() {
        const auto key = std::type_index(typeid(T));
        auto& slot = stores_[key];
        if (!slot) slot = std::make_unique<ComponentStore<T>>();
        return *static_cast<ComponentStore<T>*>(slot.get());
    }
    template <class T>
    const ComponentStore<T>& store() const {
        const auto key = std::type_index(typeid(T));
        static const ComponentStore<T> empty;
        const auto it = stores_.find(key);
        return it == stores_.end() ? empty
                                   : *static_cast<const ComponentStore<T>*>(it->second.get());
    }

    void detach_child_links(EntityId id);
    [[nodiscard]] bool is_ancestor(EntityId ancestor, EntityId descendant) const;

    EntityRegistry registry_;
    std::unordered_map<std::type_index, std::unique_ptr<ComponentStoreBase>> stores_;
    std::unordered_map<std::uint64_t, EntityId> parents_;   // child.value -> parent
    std::unordered_map<std::uint64_t, std::vector<EntityId>> children_; // parent.value -> children
    std::unordered_map<std::int64_t, EntityId> legacy_to_entity_;
    std::unordered_map<std::uint64_t, std::int64_t> entity_to_legacy_;
    std::unordered_map<std::string, Codec> codecs_;
    std::vector<EntityId> membership_;
};

} // namespace stellar::engine
