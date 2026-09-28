<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Faint system skies — 2026-09-20

The current policy supersedes the original ten-category visual selection. The
user requested quiet skies resembling the star map, with no large bright stars
or crowded background competing with planets. All 198 supplied alternatives
are excluded from local-sky selection; their original files remain intact.

## Source and appearance

The single approved plate is an exact copy of the regional star map's
`assets/visual/space/star-background.png`, stored at
`assets/visual/starfields/faint-map-reference.png`, 2944 × 1648.
SHA-256: `7e48914134e12b7498499a20202773f4382b88dc5809ee0e945f4d0b12b57406`.

Both views now share `native_starfield_style.hpp::faint_starfield_tint`
(148/255), matching the map's existing subdued display. This tint changes only
display brightness; it does not blur or overwrite the approved texture. The
full-view review caught and corrected a mismatch where the local view used the
same image at full brightness. High/Ultra retain native pixels and resolution;
Low/Medium prepare 768/1280-wide variants and may upload BC1 mip chains.

Sol always uses this plate with unity profile exposure, no rotation, mirror,
offset, blend, local cloud or cloud absorption. Other systems use only this
faint pool, with seeded half-turn/mirror and small crop variation. Their physical
region metadata remains available for simulation and separately rendered local
phenomena. Background placement is independent of orbital camera zoom/pan.

## Ownership and packaging

- Core `system_background` owns deterministic approved selection and Sol policy.
- Engine `celestial_background` owns projection and bounded image preparation.
- Application owns the shared map/system display tint, lazy loading, settings
  and the three-entry image cache. Core profile cache remains capped at 4,096.
- Runtime config version is `starfields-v2-faint-map-reference`. Old category
  debug overrides resolve back to the accepted pool, never to retired artwork.
- `export/native-environment-assets.json` and CMake copy only the accepted sky.
  Old source files are not deleted. Cooker discovery follows runtime manifests.
- `STARFIELD_ASSET_AUDIT.md` and the JSON audit record 199 reviewed entries:
  198 rejected alternatives and one accepted map reference.

## Validation and limits

`system_background` tests 48,000 deterministic profiles, audit/allowlist
membership, unchanged High/Ultra pixels, save reconstruction, Sol's clear-sky
rule, queue release on screen changes, layer order, density/quality variants and
GPU captures. `native_moons` captures the production system workspace with this
background behind six actual Sol moon families.

Evidence: `build-native/preview/sky-test-captures` and
`build-native/preview/moon-test-captures`. Earlier package timing and category
counts in historical reports do not describe this new build.

There is currently one approved source plate, so systems vary its presentation
rather than selecting unique photographs. Source detail is finite; this is a
rectilinear sky suitable for the current chart camera, not a full 360° skybox.
The complete cooked-release task remains separate and is not declared finished
by this visual correction.
