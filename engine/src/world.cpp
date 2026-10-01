#include <stellar/engine/world.hpp>

#include <algorithm>

namespace stellar::engine {

namespace {

class Writer {
public:
    void u32(std::uint32_t v) { bytes(&v, sizeof(v)); }
    void u64(std::uint64_t v) { bytes(&v, sizeof(v)); }
    void i64(std::int64_t v) { bytes(&v, sizeof(v)); }
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + size);
    }
    void string(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        bytes(s.data(), s.size());
    }
    std::vector<std::uint8_t> out;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    std::uint32_t u32() { std::uint32_t v; take(&v, sizeof(v)); return v; }
    std::uint64_t u64() { std::uint64_t v; take(&v, sizeof(v)); return v; }
    std::int64_t i64() { std::int64_t v; take(&v, sizeof(v)); return v; }
    std::string string() {
        const auto n = u32();
        if (offset_ + n > size_) throw std::runtime_error("World snapshot truncated string");
        std::string s(reinterpret_cast<const char*>(data_ + offset_), n);
        offset_ += n;
        return s;
    }
    std::vector<std::uint8_t> blob(std::uint64_t n) {
        if (offset_ + n > size_) throw std::runtime_error("World snapshot truncated blob");
        std::vector<std::uint8_t> out{data_ + offset_, data_ + offset_ + n};
        offset_ += n;
        return out;
    }
    void skip(std::uint64_t n) {
        if (offset_ + n > size_) throw std::runtime_error("World snapshot truncated skip");
        offset_ += n;
    }
    [[nodiscard]] std::size_t offset() const { return offset_; }
private:
    void take(void* out, std::size_t n) {
        if (offset_ + n > size_) throw std::runtime_error("World snapshot truncated record");
        std::memcpy(out, data_ + offset_, n);
        offset_ += n;
    }
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_{};
};

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

EntityId World::create() {
    const EntityId id = registry_.create();
    membership_.push_back(id);
    return id;
}

bool World::destroy(EntityId id, DestroyMode mode) {
    if (!alive(id)) return false;
    const auto child_list = children(id);
    if (mode == DestroyMode::DestroyDescendants) {
        for (EntityId child : child_list) destroy(child, DestroyMode::DestroyDescendants);
    } else {
        for (EntityId child : child_list) clear_parent(child);
    }
    detach_child_links(id);
    for (auto& [type, store] : stores_) store->erase(id);
    if (const auto legacy = legacy_for(id)) {
        legacy_to_entity_.erase(*legacy);
        entity_to_legacy_.erase(id.value());
    }
    std::erase(membership_, id);
    return registry_.destroy(id);
}

std::vector<EntityId> World::entities() const {
    std::vector<EntityId> out;
    out.reserve(size());
    for (EntityId id : membership_) if (alive(id)) out.push_back(id);
    return out;
}

void World::set_parent(EntityId child, EntityId parent) {
    if (child == parent) throw std::invalid_argument("World entity cannot parent itself");
    if (!alive(child) || !alive(parent))
        throw std::invalid_argument("World hierarchy requires live entities");
    if (is_ancestor(child, parent))
        throw std::invalid_argument("World hierarchy cycle rejected");
    clear_parent(child);
    parents_[child.value()] = parent;
    children_[parent.value()].push_back(child);
}

void World::clear_parent(EntityId child) {
    const auto it = parents_.find(child.value());
    if (it == parents_.end()) return;
    auto& siblings = children_[it->second.value()];
    std::erase(siblings, child);
    if (siblings.empty()) children_.erase(it->second.value());
    parents_.erase(it);
}

std::optional<EntityId> World::parent(EntityId id) const {
    const auto it = parents_.find(id.value());
    return it == parents_.end() ? std::nullopt : std::optional<EntityId>{it->second};
}

std::vector<EntityId> World::children(EntityId id) const {
    const auto it = children_.find(id.value());
    return it == children_.end() ? std::vector<EntityId>{} : it->second;
}

EntityId World::root(EntityId id) const {
    EntityId current = id;
    for (std::size_t depth = 0; depth < 4096; ++depth) {
        const auto p = parent(current);
        if (!p) return current;
        current = *p;
    }
    throw std::runtime_error("World hierarchy exceeded maximum depth");
}

bool World::is_ancestor(EntityId ancestor, EntityId descendant) const {
    EntityId current = descendant;
    for (std::size_t depth = 0; depth < 4096; ++depth) {
        const auto p = parent(current);
        if (!p) return false;
        if (*p == ancestor) return true;
        current = *p;
    }
    return false;
}

void World::detach_child_links(EntityId id) {
    clear_parent(id);
    const auto it = children_.find(id.value());
    if (it != children_.end()) {
        for (EntityId child : it->second) parents_.erase(child.value());
        children_.erase(it);
    }
}

void World::bind_legacy(EntityId id, std::int64_t legacy_id) {
    if (!alive(id)) throw std::invalid_argument("World legacy binding requires a live entity");
    if (const auto existing = entity_to_legacy_.find(id.value());
        existing != entity_to_legacy_.end())
        legacy_to_entity_.erase(existing->second);
    if (const auto existing = legacy_to_entity_.find(legacy_id);
        existing != legacy_to_entity_.end())
        entity_to_legacy_.erase(existing->second.value());
    legacy_to_entity_[legacy_id] = id;
    entity_to_legacy_[id.value()] = legacy_id;
}

std::optional<EntityId> World::entity_for_legacy(std::int64_t legacy_id) const {
    const auto it = legacy_to_entity_.find(legacy_id);
    return it == legacy_to_entity_.end() ? std::nullopt : std::optional<EntityId>{it->second};
}

std::optional<std::int64_t> World::legacy_for(EntityId id) const {
    const auto it = entity_to_legacy_.find(id.value());
    return it == entity_to_legacy_.end() ? std::nullopt : std::optional<std::int64_t>{it->second};
}

void World::clear() {
    registry_ = EntityRegistry{};
    stores_.clear();
    parents_.clear();
    children_.clear();
    legacy_to_entity_.clear();
    entity_to_legacy_.clear();
    membership_.clear();
}

namespace {
template <class Map>
std::size_t map_storage_estimate(const Map &map) noexcept {
    // Node-based unordered_map: one allocation per node (value + link) plus
    // the bucket array.
    return map.size()*(sizeof(typename Map::value_type)+2*sizeof(void*))
         + map.bucket_count()*sizeof(void*);
}
} // namespace

std::size_t World::estimated_memory_bytes() const {
    std::size_t total = registry_.memory_bytes();
    for (const auto &[key, store] : stores_) total += store->memory_bytes();
    total += map_storage_estimate(stores_);
    total += map_storage_estimate(parents_);
    total += map_storage_estimate(legacy_to_entity_);
    total += map_storage_estimate(entity_to_legacy_);
    total += map_storage_estimate(codecs_);
    for (const auto &[key, children] : children_)
        total += children.capacity()*sizeof(EntityId);
    total += map_storage_estimate(children_);
    total += membership_.capacity()*sizeof(EntityId);
    return total;
}

std::vector<std::uint8_t> World::snapshot() const {
    Writer writer;
    writer.u32(snapshot_magic);
    writer.u32(snapshot_version);
    const auto all = entities();
    writer.u32(static_cast<std::uint32_t>(all.size()));
    for (EntityId id : all) {
        writer.u32(id.index);
        writer.u32(id.generation);
        // Emit each registered component present on this entity, in a stable
        // name order so snapshots are byte-deterministic.
        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> encoded;
        for (const auto& [name, codec] : codecs_) {
            if (const void* component = codec.fetch(id))
                encoded.emplace_back(name, codec.encode(component));
        }
        std::sort(encoded.begin(), encoded.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        writer.u32(static_cast<std::uint32_t>(encoded.size()));
        for (const auto& [name, bytes] : encoded) {
            writer.string(name);
            writer.u64(bytes.size());
            writer.bytes(bytes.data(), bytes.size());
        }
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> edges;
    edges.reserve(parents_.size());
    for (const auto& [child, parent_id] : parents_) edges.emplace_back(child, parent_id.value());
    std::sort(edges.begin(), edges.end());
    writer.u32(static_cast<std::uint32_t>(edges.size()));
    for (const auto& [child, parent_value] : edges) {
        writer.u64(child);
        writer.u64(parent_value);
    }
    std::vector<std::pair<std::uint64_t, std::int64_t>> bindings;
    bindings.reserve(entity_to_legacy_.size());
    for (const auto& [entity, legacy] : entity_to_legacy_) bindings.emplace_back(entity, legacy);
    std::sort(bindings.begin(), bindings.end());
    writer.u32(static_cast<std::uint32_t>(bindings.size()));
    for (const auto& [entity, legacy] : bindings) {
        writer.u64(entity);
        writer.i64(legacy);
    }
    const auto checksum = fnv1a64(writer.out.data(), writer.out.size());
    writer.u64(checksum);
    return writer.out;
}

std::vector<std::pair<std::string, std::uint64_t>> World::component_hashes()
    const {
    // Sorted codec names → deterministic section order matching the
    // per-entity sorted encoding in snapshot().
    std::vector<const Codec*> codecs;
    codecs.reserve(codecs_.size());
    for (const auto& [name, codec] : codecs_) codecs.push_back(&codec);
    std::sort(codecs.begin(), codecs.end(),
              [](const Codec* a, const Codec* b) { return a->name < b->name; });
    const auto all = entities();
    std::vector<std::pair<std::string, std::uint64_t>> sections;
    for (const Codec* codec : codecs) {
        // Section hash: FNV-1a over (entity index+generation, encoded
        // bytes) for every entity carrying the component, in entity
        // order — the same bytes snapshot() writes under this name.
        Writer section;
        bool present = false;
        for (EntityId id : all) {
            const void* component = codec->fetch(id);
            if (!component) continue;
            present = true;
            section.u32(id.index);
            section.u32(id.generation);
            const auto bytes = codec->encode(component);
            section.bytes(bytes.data(), bytes.size());
        }
        if (present)
            sections.emplace_back(codec->name, fnv1a64(section.out.data(),
                                                       section.out.size()));
    }
    return sections;
}

void World::restore(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 16) throw std::runtime_error("World snapshot too small");
    const auto checksum = fnv1a64(bytes.data(), bytes.size() - 8);
    std::uint64_t recorded;
    std::memcpy(&recorded, bytes.data() + bytes.size() - 8, 8);
    if (recorded != checksum) throw std::runtime_error("World snapshot checksum mismatch");

    Reader reader{bytes.data(), bytes.size() - 8};
    if (reader.u32() != snapshot_magic) throw std::runtime_error("World snapshot bad magic");
    if (reader.u32() != snapshot_version) throw std::runtime_error("World snapshot version mismatch");

    clear();
    const std::uint32_t entity_count = reader.u32();
    std::unordered_map<std::uint64_t, EntityId> restored;
    for (std::uint32_t e = 0; e < entity_count; ++e) {
        const std::uint32_t index = reader.u32();
        const std::uint32_t generation = reader.u32();
        const EntityId id = create();
        restored[(static_cast<std::uint64_t>(generation) << 32) | index] = id;
        const std::uint32_t component_count = reader.u32();
        for (std::uint32_t c = 0; c < component_count; ++c) {
            const auto name = reader.string();
            const auto size = reader.u64();
            const auto it = codecs_.find(name);
            if (it == codecs_.end()) {
                reader.skip(size);
                continue;
            }
            it->second.decode_into(id, reader.blob(size));
        }
    }
    const std::uint32_t edge_count = reader.u32();
    for (std::uint32_t i = 0; i < edge_count; ++i) {
        const auto child = reader.u64();
        const auto parent_value = reader.u64();
        const auto child_it = restored.find(child);
        const auto parent_it = restored.find(parent_value);
        if (child_it != restored.end() && parent_it != restored.end())
            set_parent(child_it->second, parent_it->second);
    }
    const std::uint32_t binding_count = reader.u32();
    for (std::uint32_t i = 0; i < binding_count; ++i) {
        const auto entity = reader.u64();
        const auto legacy = reader.i64();
        if (const auto it = restored.find(entity); it != restored.end())
            bind_legacy(it->second, legacy);
    }
}

} // namespace stellar::engine
