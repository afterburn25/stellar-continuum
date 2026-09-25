#include <stellar/engine/mesh3d_loader.hpp>
#include <stellar/engine/native_geometry3d.hpp>
#include <stellar/engine/scene_components.hpp>
#include <stellar/engine/world.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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

        // component_hashes(): one section per registered type present —
        // mutating a component moves only its own section, unregistered
        // components (Health) are invisible like snapshot(), and the
        // empty world reports no sections.
        const auto before = world.component_hashes();
        check(before.size() == 2, "one section per registered type");
        std::uint64_t position_hash = 0, name_hash = 0;
        for (const auto& [name, hash] : before) {
            if (name == "position") position_hash = hash;
            if (name == "name") name_hash = hash;
        }
        check(position_hash != 0 && name_hash != 0,
              "registered sections carry hashes");
        world.get<Position>(planet)->x = 9.0;
        const auto after = world.component_hashes();
        for (const auto& [name, hash] : after) {
            if (name == "position")
                check(hash != position_hash,
                      "edited component moves its own section");
            if (name == "name")
                check(hash == name_hash,
                      "untouched component keeps its section hash");
        }
        check(World{}.component_hashes().empty(),
              "empty world has no sections");
        check(after.size() == before.size(), "section set is stable");

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
        // And identical worlds produce identical section hashes (the
        // replay verifier relies on it — world was edited above, so
        // compare again's sections against the pre-edit snapshot state).
        World pristine;
        register_codecs(pristine);
        const EntityId s3 = pristine.create();
        const EntityId p3 = pristine.create();
        pristine.add(s3, Name{"sol"});
        pristine.add(p3, Position{3.25, -1.5});
        pristine.add(p3, Name{"terra"});
        check(pristine.component_hashes() == again.component_hashes(),
              "section hashes are deterministic");

        // Forward compatibility: a snapshot carrying a component the
        // loading build does not register skips its blob cleanly instead
        // of failing — the rest of the world restores intact.
        World richer;
        register_codecs(richer);
        richer.register_component<Health>(
            "health",
            [](const Health& h) {
                return std::vector<std::uint8_t>{
                    static_cast<std::uint8_t>(h.points)};
            },
            [](const std::vector<std::uint8_t>& b) {
                return Health{b.empty() ? 0 : b.front()};
            });
        const EntityId r1 = richer.create();
        richer.add(r1, Position{7.0, 8.0});
        richer.add(r1, Health{55});
        const auto rich_bytes = richer.snapshot();
        World lean;
        register_codecs(lean);  // no "health" codec
        lean.restore(rich_bytes);
        const auto lean_entities = lean.entities();
        check(lean_entities.size() == 1, "unknown-codec snapshot restores");
        check(lean.has<Position>(lean_entities.front()) &&
                  lean.get<Position>(lean_entities.front())->x == 7.0,
              "known components restore around the skipped blob");
        check(!lean.has<Health>(lean_entities.front()),
              "the unregistered component is skipped");
    }
    // Scene components: spawn_scene builds the full component set,
    // find_entity_by_name resolves handles, and the file-backed snapshot
    // helpers round-trip the spawned world (corrupt files fail safely).
    {
        SceneEntity hero{"player", 10.f, 20.f, 64.f, 64.f,
                         30.f, -15.f, 255, 220, 60,
                         "data/logo.png", -1, 0.0f,
                         "hero", 0.0f, true};
        hero.frames = 4;
        hero.fps = 6.f;
        hero.fcols = 2;
        hero.anim_loop = false;
        hero.vfx = "ember";
        hero.rotation = 45.f;
        hero.ttl = 2.5f;
        hero.flip_x = true;
        hero.visible = false;
        hero.oneway = true;
        hero.data = "checkpoint-7";
        hero.opacity = 0.5f;
        hero.anim = "patrol";
        // A child attached to the player at a (+64,+16) authored offset.
        SceneEntity turret{"turret", 74.f, 36.f};
        turret.parent = "player";
        SceneDocument doc{
            {hero, SceneEntity{"rock", 200.f, 100.f}, turret}};
        doc.tilemaps.push_back(SceneTilemap{});
        auto &ground = doc.tilemaps.back();
        ground.tileset = "sprites/tiles.png";
        ground.tile_w = 32;
        ground.tile_h = 32;
        ground.columns = 4;
        ground.collide = true;
        ground.cells = {0, -1, -1, 0, 0, 1, 1, 0};
        // A second parallaxed decor grid — layered tilemaps each spawn
        // their own carrier entity.
        doc.tilemaps.push_back(SceneTilemap{});
        auto &deco = doc.tilemaps.back();
        deco.tileset = "sprites/deco.png";
        deco.x = 96.f;
        deco.y = -64.f;
        deco.tile_w = 16;
        deco.tile_h = 16;
        deco.columns = 8;
        deco.layer = -3;
        deco.parallax = 0.5f;
        deco.cells.assign(16, 1);
        deco.name = "decor";
        SceneAnimationDef patrol;
        patrol.id = "patrol";
        patrol.loop = "pingpong";
        patrol.tracks.push_back(
            {"x", {{0.f, 10.f}, {2.f, 200.f}}});
        patrol.tracks.push_back(
            {"opacity", {{0.f, 1.f}, {2.f, 0.25f}}});
        patrol.events.push_back({1.f, "midpoint"});
        doc.animations.push_back(patrol);
        // Document codec: animations + the entity anim field round-trip.
        const auto reparsed =
            SceneDocument::from_json(doc.to_json());
        check(reparsed.has_value(), "scene doc reparses");
        check(reparsed->animations.size() == 1 &&
                  reparsed->animations[0].id == "patrol" &&
                  reparsed->animations[0].loop == "pingpong" &&
                  reparsed->animations[0].tracks.size() == 2 &&
                  reparsed->animations[0].events.size() == 1,
              "animation def survives codec");
        check(reparsed->entities[0].anim == "patrol",
              "entity anim field survives codec");
        World world;
        register_scene_components(world);
        const auto spawned = spawn_scene(world, doc);
        check(spawned.size() == 3, "spawn_scene creates all entities");
        check(world.get<AnimTimeline>(spawned[0]) != nullptr &&
                  world.get<AnimTimeline>(spawned[0])->id == "patrol",
              "anim field spawns AnimTimeline component");
        const auto tile_es = tilemap_entities(world);
        check(tile_es.size() == 2, "each tilemap spawns its own entity");
        check(std::find(spawned.begin(), spawned.end(), tile_es[0]) ==
                      spawned.end() &&
                  std::find(spawned.begin(), spawned.end(), tile_es[1]) ==
                      spawned.end(),
              "tilemap entities stay out of the gameplay list");
        check(tilemap_entity(world).has_value() &&
                  *tilemap_entity(world) == tile_es[0],
              "tilemap_entity resolves the first map");
        check(world.get<Tilemap>(tile_es[0])->tileset ==
                      "sprites/tiles.png" &&
                  world.get<Tilemap>(tile_es[0])->columns == 4 &&
                  world.get<Tilemap>(tile_es[0])->cells.size() == 8 &&
                  world.get<Tilemap>(tile_es[0])->collide,
              "spawn_scene tilemap component");
        check(world.get<Tilemap>(tile_es[1])->tileset ==
                      "sprites/deco.png" &&
                  world.get<Tilemap>(tile_es[1])->x == 96.f &&
                  world.get<Tilemap>(tile_es[1])->y == -64.f &&
                  world.get<Tilemap>(tile_es[1])->tile_w == 16 &&
                  world.get<Tilemap>(tile_es[1])->layer == -3 &&
                  world.get<Tilemap>(tile_es[1])->cells.size() == 16,
              "spawn_scene second tilemap component");
        check(tilemap_index(world, "decor").has_value() &&
                  *tilemap_index(world, "decor") == 1,
              "named tilemap resolves its document-order index");
        check(!tilemap_index(world, "ground").has_value(),
              "unnamed tilemap has no named index");
        check(world.get<EntityName>(tile_es[1]) != nullptr &&
                  world.get<EntityName>(tile_es[1])->value == "decor",
              "tilemap name attaches EntityName to the carrier");
        const auto reparsed_tm =
            SceneDocument::from_json(doc.to_json());
        check(reparsed_tm.has_value() &&
                  reparsed_tm->tilemaps[1].name == "decor",
              "tilemap name survives the document codec");
        check(scene_from_world(world).tilemaps[1].name == "decor",
              "scene_from_world exports the tilemap name");
        const auto player = find_entity_by_name(world, "player");
        check(player.has_value() && *player == spawned[0],
              "find_entity_by_name resolves");
        check(world.get<Transform2D>(spawned[0])->x == 10.f,
              "spawn_scene transform");
        check(world.get<SpriteRef>(spawned[0])->value == "data/logo.png",
              "spawn_scene sprite ref");
        check(world.get<Layer>(spawned[0]) &&
                  world.get<Layer>(spawned[0])->value == -1,
              "spawn_scene layer");
        check(world.get<Parallax>(spawned[0]) &&
                  world.get<Parallax>(spawned[0])->value == 0.0f,
              "spawn_scene parallax");
        check(world.get<Label>(spawned[0]) &&
                  world.get<Label>(spawned[0])->value == "hero",
              "spawn_scene label");
        check(world.get<GravityScale>(spawned[0]) &&
                  world.get<GravityScale>(spawned[0])->value == 0.0f,
              "spawn_scene gravity scale");
        check(world.get<Solid>(spawned[0]) != nullptr &&
                  world.get<Solid>(spawned[1]) == nullptr,
              "spawn_scene solid flag");
        check(world.get<Anim>(spawned[0]) &&
                  world.get<Anim>(spawned[0])->frames == 4 &&
                  world.get<Anim>(spawned[0])->fps == 6.f &&
                  world.get<Anim>(spawned[0])->cols == 2 &&
                  !world.get<Anim>(spawned[0])->loop &&
                  world.get<VfxRef>(spawned[0]) != nullptr &&
                  world.get<VfxRef>(spawned[0])->name == "ember" &&
                  world.get<Anim>(spawned[1]) == nullptr,
              "spawn_scene animation");
        check(world.get<Rotation>(spawned[0]) &&
                  world.get<Rotation>(spawned[0])->value == 45.f,
              "spawn_scene rotation");
        check(world.get<Lifetime>(spawned[0]) &&
                  world.get<Lifetime>(spawned[0])->remaining == 2.5f &&
                  world.get<Lifetime>(spawned[1]) == nullptr,
              "spawn_scene lifetime");
        check(world.get<Flip>(spawned[0]) && world.get<Flip>(spawned[0])->x &&
                  !world.get<Flip>(spawned[0])->y &&
                  world.get<Flip>(spawned[1]) == nullptr,
              "spawn_scene flip");
        check(world.get<Hidden>(spawned[0]) != nullptr &&
                  world.get<Hidden>(spawned[1]) == nullptr,
              "spawn_scene hidden flag");
        check(world.get<Oneway>(spawned[0]) != nullptr &&
                  world.get<Oneway>(spawned[1]) == nullptr,
              "spawn_scene oneway flag");
        // Marker codecs emit a fixed byte — an empty struct memcpy'd
        // into the snapshot would leak uninitialized memory and make
        // replay checkpoint hashes nondeterministic.
        {
          const auto snap = world.snapshot();
          World restored;
          register_scene_components(restored);
          restored.restore(snap);
          check(restored.snapshot() == snap,
                "marker components snapshot byte-deterministically");
        }
        check(world.get<UserData>(spawned[0]) &&
                  world.get<UserData>(spawned[0])->value == "checkpoint-7" &&
                  world.get<UserData>(spawned[1]) == nullptr,
              "spawn_scene user data");
        check(world.get<Opacity>(spawned[0]) &&
                  world.get<Opacity>(spawned[0])->value == 0.5f &&
                  world.get<Opacity>(spawned[1]) == nullptr,
              "spawn_scene opacity");
        check(world.get<SpriteRef>(spawned[1]) == nullptr,
              "empty sprite leaves no SpriteRef");
        // Hierarchy: the Parent component derives its offset from the
        // authored document positions, already resolved at spawn.
        const auto *par = world.get<Parent>(spawned[2]);
        check(par != nullptr && par->name == "player" &&
                  par->off_x == 64.f && par->off_y == 16.f &&
                  par->resolved,
              "spawn_scene derives parent offset");
        // Follow: moving the parent then resolving carries the child.
        world.get<Transform2D>(spawned[0])->x = 110.f;
        world.get<Transform2D>(spawned[0])->y = 60.f;
        resolve_hierarchy(world);
        check(world.get<Transform2D>(spawned[2])->x == 174.f &&
                  world.get<Transform2D>(spawned[2])->y == 76.f,
              "child follows the resolved parent");
        // Drift: the child's own world-space edits re-bake into its
        // offset rather than being lost on the next resolve.
        world.get<Transform2D>(spawned[2])->x += 8.f;
        resolve_hierarchy(world);
        check(world.get<Parent>(spawned[2])->off_x == 72.f &&
                  world.get<Transform2D>(spawned[2])->x == 182.f,
              "child drift re-bakes into the local offset");
        world.get<Transform2D>(spawned[0])->x = 10.f;
        world.get<Transform2D>(spawned[0])->y = 20.f;
        world.get<Transform2D>(spawned[2])->x = 74.f;
        world.get<Transform2D>(spawned[2])->y = 36.f;
        world.get<Parent>(spawned[2])->last_px = 10.f;
        world.get<Parent>(spawned[2])->last_py = 20.f;
        resolve_hierarchy(world);
        check(world.get<Transform2D>(spawned[2])->x == 74.f,
              "hierarchy reset restores authored offset");

        // Mid-clip playhead state snapshots via the saved_* scratch —
        // a detached player's time/pause survives save/load verbatim.
        world.get<AnimTimeline>(spawned[0])->saved_time = 1.25f;
        world.get<AnimTimeline>(spawned[0])->saved_playing = false;
        const auto path = std::filesystem::temp_directory_path() /
                          "stellar_scene_roundtrip.stw";
        save_world_to_file(world, path);
        check(std::filesystem::is_regular_file(path), "save file written");

        // Mutate then restore: the world returns to the saved state.
        world.get<Transform2D>(spawned[0])->x = 999.f;
        world.destroy(spawned[1]);
        check(load_world_from_file(world, path), "load_world_from_file");
        const auto rep = find_entity_by_name(world, "player");
        check(rep.has_value() && world.get<Transform2D>(*rep)->x == 10.f,
              "restore revives saved transform");
        check(find_entity_by_name(world, "rock").has_value(),
              "restore revives destroyed entity");
        check(world.get<SpriteRef>(*rep)->value == "data/logo.png",
              "restore revives sprite path");
        // Attachments survive restore — the Parent codec is name-keyed so
        // it resolves against the regenerated entity ids.
        const auto rep_tur = find_entity_by_name(world, "turret");
        check(rep_tur.has_value() &&
                  world.get<Parent>(*rep_tur) != nullptr &&
                  world.get<Parent>(*rep_tur)->name == "player",
              "parent attachment survives restore");
        const auto *rep_anim = world.get<AnimTimeline>(*rep);
        check(rep_anim != nullptr && rep_anim->id == "patrol" &&
                  std::abs(rep_anim->saved_time - 1.25f) < 1e-5 &&
                  !rep_anim->saved_playing,
              "anim playhead survives restore");
        world.get<Transform2D>(*rep)->x = 60.f;
        resolve_hierarchy(world);
        check(world.get<Transform2D>(*rep_tur)->x == 124.f,
              "restored child still follows its parent");
        world.get<Transform2D>(*rep)->x = 10.f;
        // Destructible terrain: runtime cell edits survive restore.
        // (Entity ids regenerate on restore — re-resolve the carriers.)
        const auto tile_es2 = tilemap_entities(world);
        check(tile_es2.size() == 2, "restore revives every tilemap entity");
        world.get<Tilemap>(tile_es2[0])->cells[1] = 7;
        world.get<Tilemap>(tile_es2[1])->cells[0] = 9;
        save_world_to_file(world, path);
        world.get<Tilemap>(tile_es2[0])->cells[1] = -1;
        world.get<Tilemap>(tile_es2[1])->cells[0] = 4;
        check(load_world_from_file(world, path), "tilemap save reload");
        const auto tile_es3 = tilemap_entities(world);
        check(tile_es3.size() == 2 &&
                  world.get<Tilemap>(tile_es3[0])->cells[1] == 7 &&
                  world.get<Tilemap>(tile_es3[1])->cells[0] == 9,
              "every tilemap's cell edits snapshot with the world");

        // scene_from_world exports live state back to an editable document.
        const auto exported = scene_from_world(world);
        check(exported.entities.size() == 3, "scene_from_world exports all");
        const auto *ex_player = &exported.entities[0];
        for (const auto &e : exported.entities)
          if (e.name == "player") ex_player = &e;
        check(ex_player->name == "player" && ex_player->x == 10.f &&
                  ex_player->vx == 30.f &&
                  ex_player->sprite == "data/logo.png" &&
                  ex_player->layer == -1 && ex_player->parallax == 0.0f &&
                  ex_player->text == "hero" &&
                  ex_player->gravity_scale == 0.0f && ex_player->solid &&
                  ex_player->frames == 4 && ex_player->fps == 6.f &&
                  ex_player->fcols == 2 && !ex_player->anim_loop &&
                  ex_player->vfx == "ember" &&
                  ex_player->rotation == 45.f && ex_player->ttl == 2.5f &&
                  ex_player->flip_x && !ex_player->flip_y &&
                  !ex_player->visible && ex_player->oneway &&
                  ex_player->data == "checkpoint-7" &&
                  ex_player->opacity == 0.5f,
              "scene_from_world round-trips fields");
        const auto *ex_turret = &exported.entities[0];
        for (const auto &e : exported.entities)
          if (e.name == "turret") ex_turret = &e;
        check(ex_turret->parent == "player",
              "scene_from_world exports the parent link");
        check(exported.tilemaps.size() == 2 &&
                  exported.tilemaps[0].columns == 4 &&
                  exported.tilemaps[0].cells[1] == 7 &&
                  exported.tilemaps[0].tileset == "sprites/tiles.png" &&
                  exported.tilemaps[1].tileset == "sprites/deco.png" &&
                  exported.tilemaps[1].x == 96.f &&
                  exported.tilemaps[1].y == -64.f &&
                  exported.tilemaps[1].tile_w == 16 &&
                  exported.tilemaps[1].cells[0] == 9,
              "scene_from_world exports every tilemap");

        // Failure paths: absent and corrupt files return false, world intact.
        check(!load_world_from_file(
                  world, std::filesystem::temp_directory_path() /
                             "stellar_scene_missing.stw"),
              "missing save returns false");
        // A second save rotates the first into the .bak history chain.
        save_world_to_file(world, path);
        {
            std::ofstream bad(path, std::ios::binary | std::ios::trunc);
            bad << "not-a-snapshot";
        }
        // The rotated .bak chain from the earlier save recovers the world.
        world.get<Transform2D>(*rep)->x = 7777.f;
        check(load_world_from_file(world, path),
              "corrupt primary recovers from history");
        check(world.get<Transform2D>(
                  *find_entity_by_name(world, "player"))
                      ->x == 10.f,
              "history slot restores saved state");
        // With every slot gone too, load fails and the world stays intact.
        std::filesystem::remove(path);
        for (std::size_t slot = 1; slot <= 8; ++slot) {
            std::error_code ec;
            std::filesystem::remove(
                slot == 1 ? path.parent_path() /
                                (path.filename().string() + ".bak")
                          : path.parent_path() /
                                (path.filename().string() + ".bak." +
                                 std::to_string(slot)),
                ec);
        }
        world.get<Transform2D>(
            *find_entity_by_name(world, "player"))
            ->x = 5555.f;
        check(!load_world_from_file(world, path),
              "exhausted history returns false");
        check(world.get<Transform2D>(
                  *find_entity_by_name(world, "player"))
                      ->x == 5555.f,
              "failed load leaves world untouched");
    }

    // 3D scene components: spawn_scene3d builds the 3D set, codecs
    // snapshot/restore it, entities3d finds the set, resolve_hierarchy3d
    // applies parent-follow, scene3d_from_world exports it back.
    {
        World world3;
        register_scene_components(world3);
        Scene3dDocument doc;
        Scene3dEntity ship;
        ship.name = "ship";
        ship.mesh = "box:2,1,1";
        ship.x = 10.f;
        ship.y = 5.f;
        ship.z = -3.f;
        ship.yaw_deg = 90.f;
        ship.scale = 2.f;
        ship.vx = 1.f;
        ship.vy = -2.f;
        ship.vz = 0.5f;
        ship.r = 10;
        ship.g = 200;
        ship.b = 90;
        ship.texture = "models/ship.png";
        ship.solid = true;
        ship.data = "flagship";
        doc.entities.push_back(ship);
        Scene3dEntity turret;
        turret.name = "turret";
        turret.mesh = "sphere:8,4";
        turret.x = 12.f;
        turret.y = 7.f;
        turret.z = -3.f;
        turret.double_sided = true;
        turret.opacity = 0.5f;
        turret.ttl = 3.f;
        turret.parent = "ship";
        turret.metallic = 1.f;
        turret.roughness = 0.25f;
        turret.emissive = "maps/glow.png";
        turret.emissive_strength = 4.f;
        turret.emissive_r = 1.f;
        turret.emissive_g = 0.5f;
        turret.emissive_b = 0.2f;
        turret.night_emissive = 1.f;
        turret.environment = "maps/env.png";
        turret.environment_strength = 0.6f;
        turret.metallic_roughness = "maps/mr.png";
        turret.alpha_cutout = 0.4f;
        turret.uv_tile_x = 3.f;
        turret.uv_tile_y = 1.5f;
        turret.atmo_strength = 2.f;
        turret.atmo_power = 4.f;
        turret.atmo_r = 0.2f;
        turret.atmo_g = 0.5f;
        turret.atmo_b = 0.8f;
        turret.visible_range = 400.f;
        turret.normal_map = "maps/turret_n.png";
        turret.cloud_map = "maps/turret_clouds.png";
        turret.normal_strength = 0.9f;
        turret.cloud_opacity = 0.7f;
        turret.cloud_albedo = 0.8f;
        turret.cloud_offset_x = 0.1f;
        turret.cloud_offset_y = 0.2f;
        turret.terminator_wrap = 0.5f;
        turret.limb_darkening = 0.6f;
        turret.band_shear = -0.3f;
        turret.orbital_beaming = 0.65f;
        turret.star_kelvin = 5800.0;
        turret.accretion = {0.3f, 1.f, 12000.f, -0.6f};
        turret.lod_meshes = {"models/turret_mid.obj", "models/turret_low.obj"};
        turret.lod_pixels = 64.f;
        doc.entities.push_back(turret);
        const auto spawned = spawn_scene3d(world3, doc);
        check(spawned.size() == 2, "spawn_scene3d creates all entities");
        const auto ship_e = spawned[0];
        const auto turret_e = spawned[1];
        check(world3.get<Transform3D>(ship_e) != nullptr &&
                  world3.get<Transform3D>(ship_e)->x == 10.f &&
                  world3.get<Transform3D>(ship_e)->scale == 2.f,
              "spawn_scene3d transform3d");
        check(world3.get<Velocity3D>(ship_e) != nullptr &&
                  world3.get<Velocity3D>(ship_e)->dy == -2.f,
              "spawn_scene3d velocity3d");
        check(world3.get<MeshRef>(ship_e)->spec == "box:2,1,1",
              "spawn_scene3d mesh ref");
        check(world3.get<TextureRef>(ship_e)->value == "models/ship.png",
              "spawn_scene3d texture ref");
        check(world3.get<Solid>(ship_e) != nullptr,
              "spawn_scene3d solid flag");
        check(world3.get<UserData>(ship_e)->value == "flagship",
              "spawn_scene3d user data");
        check(world3.get<DoubleSided>(turret_e) != nullptr,
              "spawn_scene3d double-sided marker");
        const auto *pbr = world3.get<MaterialPbr>(turret_e);
        check(pbr != nullptr && pbr->metallic == 1.f &&
                  pbr->roughness == 0.25f && pbr->emissive == "maps/glow.png" &&
                  pbr->emissive_strength == 4.f && pbr->night_emissive == 1.f &&
                  pbr->environment == "maps/env.png" &&
                  pbr->environment_strength == 0.6f &&
                  pbr->metallic_roughness == "maps/mr.png" &&
                  pbr->alpha_cutout == 0.4f && pbr->uv_tile_x == 3.f &&
                  pbr->uv_tile_y == 1.5f,
              "spawn_scene3d materialpbr component");
        const auto *shell = world3.get<AtmosphereShell>(turret_e);
        check(shell != nullptr && shell->strength == 2.f &&
                  shell->power == 4.f && shell->r == 0.2f &&
                  shell->b == 0.8f,
              "spawn_scene3d atmosphere component");
        check(world3.get<MaterialPbr>(ship_e) == nullptr &&
                  world3.get<AtmosphereShell>(ship_e) == nullptr,
              "defaults do not attach material extensions");
        const auto *vr = world3.get<VisibleRange>(turret_e);
        check(vr != nullptr && vr->range == 400.f,
              "spawn_scene3d visible-range component");
        check(world3.get<VisibleRange>(ship_e) == nullptr,
              "unset range does not attach a component");
        const auto *ms = world3.get<MaterialSurface>(turret_e);
        check(ms != nullptr && ms->normal_map == "maps/turret_n.png" &&
                  ms->properties_map.empty() &&
                  ms->cloud_map == "maps/turret_clouds.png" &&
                  ms->normal_strength == 0.9f &&
                  ms->cloud_opacity == 0.7f && ms->cloud_albedo == 0.8f &&
                  ms->cloud_offset_x == 0.1f && ms->cloud_offset_y == 0.2f &&
                  ms->terminator_wrap == 0.5f && ms->limb_darkening == 0.6f &&
                  ms->band_shear == -0.3f && ms->orbital_beaming == 0.65f,
              "spawn_scene3d materialsurface component");
        check(world3.get<MaterialSurface>(ship_e) == nullptr,
              "defaults do not attach a surface component");
        const auto *ml = world3.get<MeshLods>(turret_e);
        check(ml != nullptr && ml->specs.size() == 2 &&
                  ml->specs[0] == "models/turret_mid.obj" &&
                  ml->specs[1] == "models/turret_low.obj" &&
                  ml->pixels == 64.f,
              "spawn_scene3d meshlods component");
        check(world3.get<MeshLods>(ship_e) == nullptr,
              "no LOD chain does not attach a component");
        const auto *sp = world3.get<StarPhotosphere>(turret_e);
        check(sp != nullptr && sp->kelvin == 5800.0,
              "spawn_scene3d starphotosphere component");
        check(world3.get<StarPhotosphere>(ship_e) == nullptr,
              "no starKelvin does not attach a component");
        const auto *ad = world3.get<AccretionDisc>(turret_e);
        check(ad != nullptr && ad->inner == 0.3f && ad->outer == 1.f &&
                  ad->kelvin == 12000.f && ad->beaming == -0.6f,
              "spawn_scene3d accretiondisc component");
        check(world3.get<AccretionDisc>(ship_e) == nullptr,
              "no accretion key does not attach a component");
        check(world3.get<Lifetime>(turret_e)->remaining == 3.f,
              "spawn_scene3d lifetime");
        const auto *pt = world3.get<Parent3D>(turret_e);
        check(pt != nullptr && pt->name == "ship" && pt->resolved &&
                  pt->off_x == 2.f && pt->off_y == 2.f && pt->off_z == 0.f,
              "spawn_scene3d derives parent offset");
        check(entities3d(world3).size() == 2, "entities3d finds the set");

        // Hierarchy: moving the parent re-anchors the child at offset.
        world3.get<Transform3D>(ship_e)->x = 20.f;
        resolve_hierarchy3d(world3);
        check(world3.get<Transform3D>(turret_e)->x == 22.f,
              "3d child follows parent x");

        // Snapshot/restore: every 3D component survives the codec round.
        const auto bytes = world3.snapshot();
        World restored;
        register_scene_components(restored);
        restored.restore(bytes);
        check(restored.size() == 2, "3d snapshot restores");
        const auto re_ship = find_entity_by_name(restored, "ship");
        check(re_ship.has_value(), "3d entity survives restore");
        if (re_ship) {
            const auto *rt = restored.get<Transform3D>(*re_ship);
            check(rt != nullptr && rt->x == 20.f && rt->z == -3.f &&
                      rt->scale == 2.f,
                  "transform3d codec round-trips");
            check(restored.get<Velocity3D>(*re_ship)->dx == 1.f,
                  "velocity3d codec round-trips");
            check(restored.get<MeshRef>(*re_ship)->spec == "box:2,1,1",
                  "meshref codec round-trips");
        }
        const auto re_turret = find_entity_by_name(restored, "turret");
        check(re_turret.has_value() &&
                  restored.get<Parent3D>(*re_turret) != nullptr,
              "parent3d attachment survives restore");
        if (re_turret) {
            const auto *rp = restored.get<MaterialPbr>(*re_turret);
            check(rp != nullptr && rp->metallic == 1.f &&
                      rp->emissive == "maps/glow.png" &&
                      rp->environment == "maps/env.png" &&
                      rp->uv_tile_x == 3.f && rp->alpha_cutout == 0.4f,
                  "materialpbr codec round-trips");
            const auto *ra = restored.get<AtmosphereShell>(*re_turret);
            check(ra != nullptr && ra->strength == 2.f && ra->power == 4.f,
                  "atmosphere codec round-trips");
            const auto *rv = restored.get<VisibleRange>(*re_turret);
            check(rv != nullptr && rv->range == 400.f,
                  "visiblerange codec round-trips");
            const auto *rms = restored.get<MaterialSurface>(*re_turret);
            check(rms != nullptr && rms->cloud_map == "maps/turret_clouds.png" &&
                      rms->normal_map == "maps/turret_n.png" &&
                      rms->cloud_albedo == 0.8f &&
                      rms->terminator_wrap == 0.5f &&
                      rms->limb_darkening == 0.6f &&
                      rms->band_shear == -0.3f &&
                      rms->orbital_beaming == 0.65f &&
                      rms->cloud_offset_y == 0.2f,
                  "materialsurface codec round-trips");
            const auto *rml = restored.get<MeshLods>(*re_turret);
            check(rml != nullptr && rml->specs.size() == 2 &&
                      rml->specs[1] == "models/turret_low.obj" &&
                      rml->pixels == 64.f,
                  "meshlods codec round-trips");
            const auto *rsp = restored.get<StarPhotosphere>(*re_turret);
            check(rsp != nullptr && rsp->kelvin == 5800.0,
                  "starphotosphere codec round-trips");
            const auto *rad = restored.get<AccretionDisc>(*re_turret);
            check(rad != nullptr && rad->inner == 0.3f &&
                      rad->kelvin == 12000.f && rad->beaming == -0.6f,
                  "accretiondisc codec round-trips");
        }
        if (re_turret) {
            resolve_hierarchy3d(restored);
            check(restored.get<Transform3D>(*re_turret)->x == 22.f,
                  "restored 3d child still follows");
        }

        // Export: live world → editable document.
        const auto out = scene3d_from_world(restored);
        check(out.entities.size() == 2, "scene3d_from_world exports all");
        check(out.entities[0].name == "ship" &&
                  out.entities[0].mesh == "box:2,1,1" &&
                  out.entities[0].x == 20.f && out.entities[0].scale == 2.f &&
                  out.entities[0].vx == 1.f && out.entities[0].solid &&
                  out.entities[0].data == "flagship",
              "scene3d_from_world round-trips fields");
        check(out.entities[1].parent == "ship",
              "scene3d_from_world exports the parent link");
        check(out.entities[1].metallic == 1.f &&
                  out.entities[1].emissive == "maps/glow.png" &&
                  out.entities[1].environment_strength == 0.6f &&
                  out.entities[1].uv_tile_x == 3.f &&
                  out.entities[1].atmo_strength == 2.f &&
                  out.entities[1].atmo_b == 0.8f &&
                  out.entities[1].visible_range == 400.f,
              "scene3d_from_world exports material extensions");
        check(out.entities[1].normal_map == "maps/turret_n.png" &&
                  out.entities[1].cloud_map == "maps/turret_clouds.png" &&
                  out.entities[1].normal_strength == 0.9f &&
                  out.entities[1].cloud_opacity == 0.7f &&
                  out.entities[1].cloud_albedo == 0.8f &&
                  out.entities[1].cloud_offset_y == 0.2f &&
                  out.entities[1].terminator_wrap == 0.5f &&
                  out.entities[1].limb_darkening == 0.6f &&
                  out.entities[1].band_shear == -0.3f &&
                  out.entities[1].orbital_beaming == 0.65f &&
                  out.entities[1].star_kelvin == 5800.0 &&
                  out.entities[1].accretion[0] == 0.3f &&
                  out.entities[1].accretion[2] == 12000.f &&
                  out.entities[1].accretion[3] == -0.6f,
              "scene3d_from_world exports surface response");
        check(out.entities[1].lod_meshes.size() == 2 &&
                  out.entities[1].lod_meshes[0] == "models/turret_mid.obj" &&
                  out.entities[1].lod_pixels == 64.f,
              "scene3d_from_world exports the LOD chain");

        // Geometry: box primitive topology + OBJ parse/malformed reject.
        const auto box = stellar::native_map::box_mesh(2.f, 1.f, 1.f);
        check(box != nullptr && box->indices().size() == 36 &&
                  box->vertices().size() == 24,
              "box_mesh produces 6 quad faces");
        const auto obj = load_obj_mesh(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        check(obj != nullptr && obj->indices().size() == 3,
              "obj loader parses a triangle");
        bool threw = false;
        try {
            (void)load_obj_mesh("v 0 0\nf 1 2 3\n");
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw, "obj loader rejects malformed input");

        // raycast_world3d: nearest triangle hit through the shared
        // resolver — rotation and scale aware, misses return nullopt.
        const auto resolve = [](const std::string &spec) {
            return resolve_mesh_spec(spec, nullptr);
        };
        check(resolve_mesh_spec("box", nullptr) != nullptr &&
                  resolve_mesh_spec("bogus", nullptr) == nullptr,
              "resolve_mesh_spec primitives and rejection");
        // Ray straight down over the ship: box:2,1,1 scaled 2, yaw 90 —
        // top face sits at y = 5 + 0.5*2 = 6 → distance 4 from y=10.
        const auto down = raycast_world3d(restored, entities3d(restored),
                                          resolve, 20.0, 10.0, -3.0,
                                          0.0, -1.0, 0.0, 100.f);
        check(down && down->entity == *re_ship &&
                  std::abs(down->distance - 4.f) < 0.01f,
              "raycast_world3d hits scaled+rotated box top");
        // The turret at (22,9,-3) is nearer along a down-ray at x=22:
        // sphere:8,4 (unit, scale 1) centered y=9 → top at y≈10 plane.
        const auto nearer = raycast_world3d(
            restored, entities3d(restored), resolve, 22.0, 12.0, -3.0,
            0.0, -1.0, 0.0, 100.f);
        check(nearer && nearer->entity == *re_turret,
              "raycast_world3d picks the nearer entity");
        check(!raycast_world3d(restored, entities3d(restored), resolve,
                               0.0, 10.0, 50.0, 0.0, -1.0, 0.0, 100.f),
              "raycast_world3d miss returns nullopt");
        check(!raycast_world3d(restored, entities3d(restored), resolve,
                               0.0, 10.0, -3.0, 0.0, 0.0, 0.0, 100.f),
              "raycast_world3d rejects a degenerate ray");
    }

    if (failures != 0) {
        std::cerr << failures << " world checks failed\n";
        return 1;
    }
    std::cout << "World entity/hierarchy/component/serialization tests passed\n";
    return 0;
}
