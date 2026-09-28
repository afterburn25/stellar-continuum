<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Galaxy artwork mapping

12 mapped images; 6 complete morphology-only pairs.
No explicit population-state images were supplied: 0/30 exact state pairs, 30/30 covered by validated same-morphology generic pairs.
No duplicate or ambiguous mappings.

| Source filename | Morphology | Population | Variant | Mapped |
| --- | --- | --- | --- | --- |
| Barred Spiral galaxy — gas-dust only.png | barred_spiral | Generic | gas_dust_only | Yes |
| Barred Spiral galaxy — stars included.png | barred_spiral | Generic | stars_included | Yes |
| Elliptical galaxy — gas-dust only.png | elliptical | Generic | gas_dust_only | Yes |
| Elliptical galaxy — stars included.png | elliptical | Generic | stars_included | Yes |
| Irregular galaxy — gas-dust only.png | irregular | Generic | gas_dust_only | Yes |
| Irregular galaxy — stars included.png | irregular | Generic | stars_included | Yes |
| Lenticular galaxy — gas-dust only.png | lenticular | Generic | gas_dust_only | Yes |
| Lenticular galaxy — stars included.png | lenticular | Generic | stars_included | Yes |
| Ring galaxy — gas-dust only.png | ring | Generic | gas_dust_only | Yes |
| Ring galaxy — stars included.png | ring | Generic | stars_included | Yes |
| Spiral galaxy — gas-dust only.png | spiral | Generic | gas_dust_only | Yes |
| Spiral galaxy — stars included.png | spiral | Generic | stars_included | Yes |

Preview selection uses Stars Included only. Map structure uses Gas-Dust Only only. Both sides of a state fallback resolve to the same generic pair.
Density masks: cached 96 × 96 luminance grids, embedded in Core configuration; source files unchanged. Runtime art is aspect-preserving, bounded to 1280 pixels on its longest side.
Exact population variants can be added without changing stable morphology IDs.

Irregular and Spiral pairs were reframed to 16:9 using the built-in image-editing tool, as requested. Their runtime images are 1280 × 720. The original files remain unchanged. Full-resolution edited sources and hashes are recorded in `export/galaxy-asset-edits.json`; prompts and provenance are in [the widescreen artwork report](GALAXY_WIDESCREEN_ART.md).
