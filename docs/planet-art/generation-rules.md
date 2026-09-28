# Planet generation rules and complete taxonomy

Configuration: `data/planets/planet-types-v1.json`. The native build embeds this file; edit and rebuild to change weights. These are game priors, not measured cosmic occurrence rates. Mass, radius, atmosphere and stellar exposure are generated/checked before approved art selection. Eligibility is normalized among physically viable classes; the final population therefore does not have these exact percentages. Existing colonies and legacy saves retain their physical state.

## Broad classes

| Class | Baseline | Mass (Earth) | Radius (Earth) | Temperature K | Pressure kPa | Approved art | Orbital/star rules |
|---|---:|---:|---:|---:|---:|---:|---|
| Barren / Airless Rocky | 20% | 0.0001–12 | 0.03–2.2 | 3–1900 | 0–0.05 | 54 | Broad hot/cold range; airless; no ocean imagery. |
| Desert | 10% | 0.03–10 | 0.2–2.2 | 160–720 | 0–200 | 35 | Dry terrain; warm 285–450 K favored 1.5×, other eligible temperatures 0.6×. |
| Frozen / Ice | 12% | 0.0001–12 | 0.03–2.2 | 3–260 | 0–180 | 52 | Cold equilibrium; periapsis ice-heating veto; beyond snow line 2.4×, inside 0.65×. |
| Ocean / Water | 5% | 0.35–12 | 0.65–2.3 | 273.16–345 | 40–450 | 40 | Liquid-water test; HZ 2×, outside HZ 0.4×; stellar habitability and retention modifiers. |
| Temperate / Terran-like | 4% | 0.4–4 | 0.7–1.7 | 273.16–325 | 55–180 | 65 | Liquid-water test; HZ 2×, outside HZ 0.4×; stellar habitability and retention modifiers. |
| Cold Terrestrial / Tundra | 5% | 0.25–8 | 0.6–2 | 240–285 | 30–180 | 42 | Cool surface; HZ 2×, outside HZ 0.4×; liquid regions only when water is stable. |
| Volcanic / Magma / Inferno | 7% | 0.001–15 | 0.08–2.4 | 3–2700 | 0–400 | 54 | Stellar heat, eccentric-moon tides, young geology or recent impact required. Age <800 Myr 2.4×, older 0.6×; >950 K 2×. |
| Greenhouse / Toxic | 5% | 0.3–15 | 0.65–2.4 | 350–1200 | 1000–10000 | 122 | Pressure and greenhouse temperature jointly solved; incident flux >2 Sol 2×, otherwise 0.5×. |
| Carbon World | 2% | 0.03–15 | 0.2–2.4 | 3–1600 | 0–50 | 31 | Carbon visual/composition family; dry, water-free source matching and temperature constraints. |
| Super-Earth | 8% | 1.5–15 | 1–2.4 | 3–1400 | 0–700 | 42 | Mass/radius eligible dense solid; dry, habitable or cold-oceanic subclass selected after climate. |
| Mini-Neptune | 7% | 2–25 | 1.7–4 | 25–1000 | 1000–30000 | 5 | Non-solid low-radius envelope; inside snow line requires explicit migration history. |
| Gas Giant | 7% | 45–4000 | 5–15 | 20–1100 | 1000–50000 | 81 | Non-solid giant; interior formation-location mismatch requires explicit migration history. |
| Ice Giant | 5% | 7–55 | 2.5–6 | 20–230 | 1000–30000 | 24 | Cold non-solid volatile world; beyond snow line 2.4×, inside 0.65× and migration required. |
| Hot Jupiter | 1% | 60–4000 | 7–20 | 800–3500 | 1000–50000 | 0 | Hot gas envelope; explicit inward-giant-migration history, sufficient gas retention. |
| Chthonian / Stripped Core | 0.5% | 1–25 | 0.8–2.5 | 650–3000 | 0–0.01 | 0 | Hot rocky remnant; stripped-envelope history; flux >2 Sol 2×, otherwise 0.5×. |
| Cracked / Shattered | 1.5% | 0.001–15 | 0.1–2.5 | 3–2200 | 0–15 | 18 | Catastrophic impact history and authoritative cracked flag; young/evolved systems 2.5×; debris field integration. |

Total baseline: **100%**, unchanged from the requested table.

All thick solid atmospheres require a conservative retention parameter of at least 12; gas envelopes require 6. Molecular mass, gravity, irradiation-heated exobase, wind and stellar age enter that estimate. It is an eligibility heuristic, not an atmospheric escape integration. The developer index exposes temperature, pressure, incident flux, retention and heat history.

Temperate conditional group priors: Terran 55%, Jungle 15%, Savannah 15%, Gaia 5%, other temperate 10%. Each group is split equally among its eligible subtypes; groups with no physically eligible subtype are excluded before normalization. Other broad classes initially divide their subtype choice equally among eligible subtypes. Artwork counts do not increase type frequency.

## Every supported subclass

Temperature limits below are intersected with the parent class limits. Pressure/mass/radius and orbital/star rules are inherited from the parent table; water, ice and vegetation add the visible-surface constraints. Local geology requires a recorded heat mechanism, global magma requires sufficient total heat, and vegetation requires an oxygen-bearing environment in this game model. Counts are primary assignments, without double counting secondary compatibility.

| Class | Subclass / ID | Temperature K | Water / ice / vegetation | Clouds | Emission | Rings | Composition | Heat / history | Art |
|---|---|---:|---:|---:|---:|---|---|---|---:|
| barren | Ancient Grey Crater / `ancient-grey-crater` | 3–1900 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 8 |
| barren | Dormant Volcanic / `dormant-volcanic` | 3–1900 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 8 |
| barren | Iron Rich Rust / `iron-rich-rust` | 3–1900 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 16 |
| barren | Mineral Highlands / `mineral-highlands` | 3–1900 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 22 |
| carbon | Diamond Rich / `diamond-rich` | 3–1600 | 0 / 0 / 0 | 0.05 | 0 | no | carbon | stellar / undisturbed | 12 |
| carbon | Dormant Carbon Volcanic / `dormant-carbon-volcanic` | 3–1600 | 0 / 0 / 0 | 0.05 | 0 | no | carbon | stellar / undisturbed | 8 |
| carbon | Graphite / `graphite` | 3–1600 | 0 / 0 / 0 | 0.05 | 0 | no | carbon | stellar / undisturbed | 11 |
| chthonian | Stripped Core / `stripped-core` | 650–3000 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / atmosphere-loss | 0 |
| cracked | Ancient Collision Scar / `ancient-collision-scar` | 3–2200 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / catastrophic-impact | 8 |
| cracked | Blown Apart / `blown-apart` | 3–2200 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / catastrophic-impact | 0 |
| cracked | Global Fracture / `global-fracture` | 3–2200 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / catastrophic-impact | 7 |
| cracked | Partially Shattered / `partially-shattered` | 3–2200 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / catastrophic-impact | 3 |
| desert | Golden Dune / `golden-dune` | 160–720 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 11 |
| desert | Red Canyon / `red-canyon` | 160–720 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 18 |
| desert | White Salt / `white-salt` | 160–720 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / evaporated-basin | 6 |
| frozen | Blue Glacier / `blue-glacier` | 3–260 | 0 / 0.85 / 0 | 0.05 | 0 | no | water-rich | stellar / undisturbed | 17 |
| frozen | Cryogenic Hydrocarbon / `cryogenic-hydrocarbon` | 80–120 | 0 / 0.35 / 0 | 0.05 | 0 | no | water-rich | stellar / undisturbed | 0 |
| frozen | Dirty Ice / `dirty-ice` | 3–260 | 0 / 0.85 / 0 | 0.05 | 0 | no | water-rich | stellar / undisturbed | 18 |
| frozen | Global Ice / `global-ice` | 3–260 | 0 / 0.85 / 0 | 0.05 | 0 | no | water-rich | stellar / undisturbed | 17 |
| gas-giant | Amber Cloud / `amber-cloud` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 23 |
| gas-giant | Blue Cloud Giant / `blue-cloud-giant` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 1 |
| gas-giant | Chemical Cloud / `chemical-cloud` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 8 |
| gas-giant | Copper Ring Giant / `copper-ring-giant` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | yes | hydrogen-helium | stellar / undisturbed | 8 |
| gas-giant | Cream Cloud Giant / `cream-cloud-giant` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 3 |
| gas-giant | Lavender Cloud Giant / `lavender-cloud-giant` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 24 |
| gas-giant | Pale Ring Giant / `pale-ring-giant` | 20–1100 | 0 / 0 / 0 | 0.8 | 0 | yes | hydrogen-helium | stellar / undisturbed | 14 |
| greenhouse | Acid Rain / `acid-rain` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 19 |
| greenhouse | Amber Chemical / `amber-chemical` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 9 |
| greenhouse | Amber Cloud / `amber-cloud` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 5 |
| greenhouse | Blue Green Chemical / `blue-green-chemical` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 12 |
| greenhouse | Corrosive Cloud / `corrosive-cloud` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 20 |
| greenhouse | Cream Cloud / `cream-cloud` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 22 |
| greenhouse | Green Chemical / `green-chemical` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 15 |
| greenhouse | Sulfuric Cloud / `sulfuric-cloud` | 350–1200 | 0 / 0 / 0 | 0.8 | 0 | no | silicate | stellar / undisturbed | 20 |
| hot-jupiter | Migrated Hot Giant / `migrated-hot-giant` | 800–3500 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 0 |
| ice-giant | Blue Cloud Giant / `blue-cloud-giant` | 20–230 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 5 |
| ice-giant | Blue Ring Giant / `blue-ring-giant` | 20–230 | 0 / 0 / 0 | 0.8 | 0 | yes | hydrogen-helium | stellar / undisturbed | 19 |
| mini-neptune | Blue Cloud / `blue-cloud` | 25–1000 | 0 / 0 / 0 | 0.8 | 0 | no | hydrogen-helium | stellar / undisturbed | 5 |
| ocean | Archipelago / `archipelago` | 273.16–340 | 0.75 / 0 / 0.04 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 13 |
| ocean | Global Waterworld / `global-waterworld` | 273.16–340 | 0.75 / 0 / 0 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 11 |
| ocean | Storm Ocean / `storm-ocean` | 273.16–340 | 0.75 / 0 / 0 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 16 |
| super-earth | Cold Oceanic / `cold-oceanic` | 273.16–286 | 0.45 / 0.4 / 0 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 13 |
| super-earth | Greenhouse / `greenhouse` | 350–1200 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 0 |
| super-earth | Massive Habitable / `massive-habitable` | 278–320 | 0.45 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 13 |
| super-earth | Rocky High Gravity / `rocky-high-gravity` | 3–1400 | 0 / 0 / 0 | 0.05 | 0 | no | silicate | stellar / undisturbed | 16 |
| temperate | Gaia Balanced / `gaia-balanced` | 273.16–325 | 0.5 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 13 |
| temperate | Gaia Islands / `gaia-islands` | 273.16–325 | 0.5 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 11 |
| temperate | Gaia Supercontinent / `gaia-supercontinent` | 273.16–325 | 0.5 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 14 |
| temperate | Jungle Archipelago / `jungle-archipelago` | 273.16–325 | 0.5 / 0 / 0.7 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 13 |
| temperate | Jungle Supercontinent / `jungle-supercontinent` | 273.16–325 | 0.5 / 0 / 0.7 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 7 |
| temperate | Other Temperate / `other-temperate` | 273.16–325 | 0.5 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 7 |
| temperate | Savannah / `savannah` | 273.16–325 | 0.5 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 0 |
| temperate | Terran / `terran` | 273.16–325 | 0.5 / 0 / 0.35 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 0 |
| tundra | Boreal / `boreal` | 273.16–285 | 0.1 / 0.4 / 0.12 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 4 |
| tundra | Cold Oceanic / `cold-oceanic` | 273.16–286 | 0.1 / 0.4 / 0 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 8 |
| tundra | Cold Steppe / `cold-steppe` | 273.16–285 | 0.1 / 0.4 / 0.12 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 15 |
| tundra | Semi Frozen / `semi-frozen` | 273.16–285 | 0.1 / 0.4 / 0 | 0.3 | 0 | no | water-rich | stellar / undisturbed | 15 |
| volcanic | Ash Caldera / `ash-caldera` | 3–2700 | 0 / 0 / 0 | 0.05 | 0.08 | no | silicate | local-geology / undisturbed | 2 |
| volcanic | Fractured Lava / `fractured-lava` | 950–2700 | 0 / 0 / 0 | 0.05 | 0.7 | no | silicate | global-magma / undisturbed | 24 |
| volcanic | Magma Ocean / `magma-ocean` | 950–2700 | 0 / 0 / 0 | 0.05 | 0.7 | no | silicate | global-magma / undisturbed | 11 |
| volcanic | Rift / `rift` | 3–2700 | 0 / 0 / 0 | 0.05 | 0.08 | no | silicate | local-geology / undisturbed | 3 |
| volcanic | Shield Volcano / `shield-volcano` | 3–2700 | 0 / 0 / 0 | 0.05 | 0.08 | no | silicate | local-geology / undisturbed | 5 |
| volcanic | Sulfur Inferno / `sulfur-inferno` | 950–2700 | 0 / 0 / 0 | 0.05 | 0.7 | no | silicate | global-magma / undisturbed | 3 |
| volcanic | Sulfur Volcanic / `sulfur-volcanic` | 3–2700 | 0 / 0 / 0 | 0.05 | 0.08 | no | silicate | local-geology / undisturbed | 6 |
| frozen | Ice World with Local Hotspots / `icy-hotspots` | 70–250 | 0 / 0.85 / 0 | 0.05 | 0.06 | no | water-ice-and-silicate | local-geology / local-geology | 0 |

Zero-art subtypes use deterministic procedural materials through the same 3D pipeline. No rejected source enters those fallbacks. The procedural ice-hotspot subtype keeps active areas localized. Supplied ring images contribute only usable central-globe surface color; separate annuli reconstruct the rings.

## Thermal equations and boundaries

`flux = luminositySolar / orbitAU²`; `Teq = max(3 K, 278.5 K × [flux × (1 − Bond albedo)]^¼)`. The inherited HZ uses stellar properties and approximately √luminosity scaling. The snow-line approximation is `2.7 AU × √luminositySolar`. Internal heat enters the fourth-power radiative balance; a pressure-dependent greenhouse multiplier then estimates mean surface temperature.

Ordinary liquid-water artwork requires 273.16–350 K, pressure above the triple-point threshold and above 1.25× the estimated saturation pressure. This deliberately excludes steam/supercritical interpretations. Ordinary extensive ice is rejected above freezing and when periapsis absorbed heating is incompatible. Eccentric frozen moons may have localized tidal hotspots with a cold mean surface.

Developer examples use actual campaign stars, obey stellar-surface/tidal-disruption limits and avoid occupied orbital radii. They do not move or replace existing bodies. Full Content Coverage creates 65 subclass examples where eligible host stars exist.

Physical background: [NASA habitable-zone overview](https://science.nasa.gov/exoplanets/habitable-zone/), [NASA atmospheric-retention context](https://science.nasa.gov/mission/webb/science-overview/science-explainers/can-rocky-worlds-orbiting-red-dwarf-stars-maintain-atmospheres/), [NASA snow-line discussion](https://astrobiology.nasa.gov/news/rethinking-the-snow-line/). Numerical gameplay coefficients above are project choices, not values inferred from those sources.
