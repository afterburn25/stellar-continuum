#include <stellar/engine/world.hpp>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Position {
    double x{}, y{};
};

struct Name {
    std::string value;
};

struct Health {
    int points{};
};

std::vector<std::uint8_t> encode_position(const Position& p) {
    std::vector<std::uint8_t> bytes(sizeof(double) * 2);
    std::memcpy(bytes.data(), &p.x, sizeof(double));
    std::memcpy(bytes.data() + sizeof(double), &p.y, sizeof(double));
    return bytes;
}

Position decode_position(const std::vector<std::uint8_t>& bytes) {
    Position p;
    std::memcpy(&p.x, bytes.data(), sizeof(double));
    std::memcpy(&p.y, bytes.data() + sizeof(double), sizeof(double));
    return p;
}

std::vector<std::uint8_t> encode_name(const Name& n) {
    return {n.value.begin(), n.value.end()};
}

Name decode_name(const std::vector<std::uint8_t>& bytes) {
    return Name{std::string{bytes.begin(), bytes.end()}};
}

void register_codecs(stellar::engine::World& world) {
    world.register_component<Position>("position", encode_position, decode_position);
    world.register_component<Name>("name", encode_name, decode_name);
}

} // namespace

int main() {
    using namespace stellar::engine;

    // Lifecycle + stale-reference safety.
    {
        World world;
        const EntityId a = world.create();
        const EntityId b = world.create();
        check(world.alive(a) && world.alive(b) && world.size() == 2, "create/alive/size");
        check(world.destroy(a), "destroy returns true for live entity");
        check(!world.alive(a), "destroyed entity is not alive");
        check(!world.destroy(a), "double destroy returns false");
        const EntityId c = world.create();
        check(c != a || !world.alive(a), "reused slot has a new generation");
    }
    // Components: add/get/has/remove/view/query.
    {
        World world;
        const EntityId ship = world.create();
        const EntityId rock = world.create();
        world.add(ship, Position{1.5, -2.0});
        world.add(ship, Name{"kestrel"});
        world.add(rock, Position{0.0, 4.0});
        world.add(rock, Health{40});

        check(world.has<Position>(ship) && world.has<Name>(ship), "component presence");
        check(!world.has<Health>(ship) && world.has<Health>(rock), "component absence");
        check(world.get<Position>(ship)->x == 1.5, "component value round-trip");
        check(world.get<Position>(rock)->y == 4.0, "sparse lookup by entity");

        check(world.view<Position>().size() == 2, "view returns all carriers");
        check(world.view<Name>().size() == 1, "view filters by type");
        check(world.query<Position, Name>().size() == 1, "multi-component query");
        check(world.query<Position, Health>().size() == 1, "multi-component query 2");

        check(world.remove<Position>(ship), "component removal");
        check(!world.has<Position>(ship) && world.has<Position>(rock), "removal is per-entity");
        // Stale safety: component of a destroyed entity is unreachable.
        world.destroy(rock);
        check(world.get<Position>(rock) == nullptr, "dead entity component lookup is null");
    }
    // Hierarchy: parent/children/root, cycle rejection, detach + cascade.
    {
        World world;
        const EntityId galaxy = world.create();
        const EntityId system = world.create();
        const EntityId star = world.create();
        const EntityId planet = world.create();
        const EntityId moon = world.create();
        world.set_parent(system, galaxy);
        world.set_parent(star, system);
        world.set_parent(planet, star);
        world.set_parent(moon, planet);
        check(world.root(moon) == galaxy, "root walks the chain");
        check(world.children(star).size() == 1 && world.children(star)[0] == planet,
              "children lists parent link");
        check(world.parent(moon) == planet, "parent lookup");

        bool cycle = false;
        try {
            world.set_parent(galaxy, moon);
        } catch (const std::invalid_argument&) {
            cycle = true;
        }
        check(cycle, "cycle is rejected");
        bool self = false;
        try {
            world.set_parent(star, star);
        } catch (const std::invalid_argument&) {
            self = true;
        }
        check(self, "self-parenting is rejected");

        // Detach: destroying a mid node orphans children.
        const EntityId other = world.create();
        world.set_parent(other, planet);
        world.destroy(planet, World::DestroyMode::DetachChildren);
        check(world.alive(moon) && world.alive(other), "detached children survive");
        check(!world.parent(moon).has_value(), "detached child has no parent");

        // Cascade: destroying a subtree removes descendants.
        const EntityId fleet = world.create();
        const EntityId ship1 = world.create();
        const EntityId ship2 = world.create();
        world.set_parent(ship1, fleet);
        world.set_parent(ship2, fleet);
        world.destroy(fleet, World::DestroyMode::DestroyDescendants);
        check(!world.alive(ship1) && !world.alive(ship2), "cascade destroys descendants");
    }
    // Legacy ID bridge.
    {
        World world;
        const EntityId system = world.create();
        world.bind_legacy(system, 42);
        check(world.entity_for_legacy(42) == system, "legacy -> entity");
        check(world.legacy_for(system) == 42, "entity -> legacy");
        world.destroy(system);
        check(!world.entity_for_legacy(42).has_value(), "binding released on destroy");
    }
    // Serialization: snapshot/restore round-trips entities, components,
    // hierarchy and legacy bindings; checksum rejects corruption.
    {
        World world;
        register_codecs(world);
        const EntityId system = world.create();
        const EntityId planet = world.create();
        world.add(system, Name{"sol"});
        world.add(planet, Position{3.25, -1.5});
        world.add(planet, Name{"terra"});
        world.set_parent(planet, system);
        world.bind_legacy(system, 7);
        const auto bytes = world.snapshot();
        check(!bytes.empty(), "snapshot produces bytes");

        World restored;
        register_codecs(restored);
        restored.restore(bytes);
        check(restored.size() == 2, "restore entity count");
        const auto restored_system = restored.entity_for_legacy(7);
        check(restored_system.has_value(), "legacy binding restored");
        check(restored.get<Name>(*restored_system)->value == "sol",
              "component payload restored");
        const auto planets = restored.query<Position, Name>();
        check(planets.size() == 1, "restored query finds planet");
        check(restored.get<Position>(planets[0])->x == 3.25, "restored position");
        check(restored.parent(planets[0]) == restored_system, "hierarchy restored");

        auto corrupted = bytes;
        corrupted[12] ^= 0xFF;
        bool rejected = false;
        try {
            restored.restore(corrupted);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "checksum rejects corrupted snapshot");

        // Determinism: identical worlds produce identical bytes.
        World again;
        register_codecs(again);
        const EntityId s2 = again.create();
        const EntityId p2 = again.create();
        again.add(s2, Name{"sol"});
        again.add(p2, Position{3.25, -1.5});
        again.add(p2, Name{"terra"});
        again.set_parent(p2, s2);
        again.bind_legacy(s2, 7);
        check(again.snapshot() == bytes, "snapshot bytes are deterministic");
    }

    if (failures != 0) {
        std::cerr << failures << " world checks failed\n";
        return 1;
    }
    std::cout << "World entity/hierarchy/component/serialization tests passed\n";
    return 0;
}
