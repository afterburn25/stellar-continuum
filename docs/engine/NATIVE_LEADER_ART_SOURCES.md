# Native leader artwork sources

The C++ client reuses these existing Stellar Continuum images unchanged:

- `assets/visual/leaders/planetary-governor.jpg`: approved Terran leadership council portrait for Civil Administration (planetary development and public services).
- `assets/visual/leaders/chief-scientist.jpg`: approved council portrait for the Science Directorate (research institutions and discovery).
- `assets/visual/leaders/fleet-commander.jpg`: approved council portrait for Fleet Command (exploration, defense and fleet operations).

All three were generated for this project with OpenAI's built-in image generation tool on 2026-09-09 using text-free, role-specific near-future human portrait prompts. Full provenance is recorded in `docs/ASSET_MANIFEST.md` ("Species and leader portrait provenance"). The portraits are decorative identity art — leader gameplay effects still require an authoritative leader system.

The native empire overview renders them as the LEADERSHIP COUNCIL section, matching the reference `BuildLeadershipCouncil` in `PlayerControls.cs`. Only these three JPEGs and this source note are declared by `export/native-leader-art-assets.json`; packaging verifies their paths and SHA-256 fingerprints.
