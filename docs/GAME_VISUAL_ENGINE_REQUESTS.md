# Game Visual Engine Requests

Requests from the **game/UI workstream** (`game/ui-visual-overhaul`) for reusable
renderer or engine capabilities that the game-facing client needs but must not
build itself. The concurrent graphics session (`engine/space-graphics-overhaul`)
owns `engine/`, `engine/shaders/`, generic rendering, and engine visual APIs.

Rules:

- Record a request here instead of building a parallel renderer or duplicating
  engine architecture in `app/native_client/`.
- Keep working on other UI/UX items while a request is pending; when a capability
  lands in `engine/space-strategy-simulation-specialization`, merge the shared
  branch into `game/ui-visual-overhaul` and consume it deliberately through the
  public engine interface.
- Do not request game-specific behavior in engine APIs; describe the reusable
  capability and the game's consumption point.
- When a request is fulfilled, move the entry to **Delivered** with the landing
  commit/PR and the consuming change.

## Entry template

```
### REQUEST: <short name>
Status:        OPEN | IN-PROGRESS (engine lane) | DELIVERED | DECLINED
Requested:    <date>
WHY NEEDED:
  <the player-facing problem or visual quality gap>
CURRENT GAME SCREEN:
  <module/surface that would consume it, e.g. app/native_client/native_scene3d.cpp galaxy view>
DESIRED PUBLIC API:
  <the engine-facing capability, e.g. "Scene3D: per-mesh atmosphere toggle" —
  describe behavior, not implementation>
PERFORMANCE CONSTRAINT:
  <frame budget / memory bound / determinism or save impact>
FALLBACK IF NOT AVAILABLE:
  <what the game will do meanwhile>
```

## Open requests

(none yet)

## Delivered

(none yet)
