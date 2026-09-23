#pragma once

#include "stellar/engine/content_resolver.hpp"
#include "stellar/engine/scene_components.hpp"
#include "stellar/engine/world.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace stellar::native_map {
class Window;
struct DrawList;
struct InputEvent;
} // namespace stellar::native_map

namespace stellar::engine {
namespace audio {
class AudioOutput;
} // namespace audio

struct RuntimeHostOptions {
  // Base package namespace (e.g. "game.my-game"); its content/ directory is
  // the loose-content root and it is protected against mod overrides.
  std::string package_id;
  std::string window_title{"Stellar Game"};
  int width{1280}, height{720};
  bool fullscreen{false};
  // Working root for packages/, mods/, build/cooked/ and editor/.
  std::filesystem::path project_root{"."};
  // Polled for changes; saving it hot-reloads the running scene.
  std::string scene_file{"editor/scene.json"};
  std::string save_file{"saves/quicksave.stw"};
  // Optional clips resolved through the ContentResolver.
  std::string music_clip{"audio/music.wav"};
  std::string music_clip_alt{"audio/music.mp3"};
  std::string bounce_clip{"audio/bounce.wav"};
  std::string bounce_clip_alt{"audio/bounce.mp3"};
};

// A ready-made windowed 2D game host: owns the Window, package/content
// resolution, the ECS World, scene-document hot reload, WASD/arrow 'player'
// input, velocity integration + wall bounce, sprite rendering (cooked BC7 or
// loose images), audio playback and F5/F9 world quicksave/quickload. Games
// customize through the callbacks rather than reimplementing the loop — the
// same role Unreal's GameInstance plays for its projects.
class RuntimeHost {
public:
  explicit RuntimeHost(RuntimeHostOptions options);
  ~RuntimeHost();
  RuntimeHost(RuntimeHost &&) noexcept;
  RuntimeHost &operator=(RuntimeHost &&) noexcept;
  RuntimeHost(const RuntimeHost &) = delete;
  RuntimeHost &operator=(const RuntimeHost &) = delete;

  [[nodiscard]] World &world();
  [[nodiscard]] const ContentResolver &content() const;
  [[nodiscard]] audio::AudioOutput &audio();
  // The entity named "player" in the active scene, if any.
  [[nodiscard]] std::optional<EntityId> player() const;

  // Runs each rendered frame after input handling and scene polling, before
  // the built-in velocity integration. The place for game logic.
  std::function<void(World &, float dt)> on_update;
  // Observed after the host's own handling (Escape/F5/F9/held keys).
  std::function<void(const native_map::InputEvent &)> on_event;
  // Appended under the built-in status line when non-empty.
  std::function<std::string()> on_status;
  // Extra overlay primitives each frame, drawn above the scene entities.
  std::function<void(native_map::DrawList &, float w, float h)> on_draw;

  // Owns the SDL loop; returns the process exit code.
  int run();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace stellar::engine
