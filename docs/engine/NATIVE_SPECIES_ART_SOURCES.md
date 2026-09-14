# Native species portrait sources

The native setup screen reuses the four square identity portraits already
registered in docs/ASSET_MANIFEST.md. They were generated for Stellar Continuum
with OpenAI's image generation tool on 2026-09-09 from the project's authored
species morphology, habitat and perception facts. They remain production
candidates, not a claim of finished artwork or cinematic communications scenes.

- terran-baseline.jpg: Terran Baseline
- pelagic-high-pressure.jpg: Pelagic High-Pressure
- compact-high-gravity.jpg: Compact High-Gravity
- cryogenic-hydrocarbon.jpg: Cryogenic Hydrocarbon

Source and runtime paths are assets/visual/species/ for these exact four JPEGs.
The native exporter pins their bytes in export/native-species-assets.json and
includes this source note as Licenses/Species-visual-sources.md. It does not copy
the surrounding directory, Godot imports or unrelated generated artwork.
The interface contain-fits the complete portrait in both the species list and
selected-species panel; it does not crop faces or stretch image proportions.
