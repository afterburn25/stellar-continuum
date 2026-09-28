# Human origin and Sol starting catalog

Fresh campaigns, including Play Demo, create one human founding civilization on Earth in Sol. The existing stable physiology ID remains `terran_baseline`; it is not reassigned on existing saves. Civilization 0 is the pre-warp human player. Its normal population, infrastructure, credits, industry, technology prerequisites and physical-ship requirements remain unchanged.

Other founding factions use the existing three nonhuman physiology profiles and each has its own distinct, deterministically planned, naturally viable home system and body. Their systems receive deterministic faction names. Reusing a physiology profile does not create a shared political identity or claim that those factions all originate on one planet. This milestone does not add race/faction identity architecture or reduce the requested civilization count.

## Canonical catalog

`StarSystemState.CatalogPresetId = "sol-v1"` is the explicit versioned catalog identity. The physical generator never infers it from display names. Sol reserves system ID 0 at regional position (0, 0); a Standard-star quota entry is swapped into that slot, retaining archetype counts. Fresh-generation settings must allocate at least one Standard star.

Mercury through Neptune have IDs 1 through 8 and orbital indices 0 through 7. Earth is body 3. Earth's Moon is body 9, with parent 3. Pluto is dwarf planet body 10 at primary orbit index 8. Its broad display orbit records eccentricity 0.2444 and inclination 17.16 degrees. These two orbital fields default to zero for older saves and procedural bodies. All other systems retain the existing per-system body-ID stride and procedural generation; planet names follow their persisted star names. The canonically authored solar bodies bypass environmental diversity conditioning. Unknown catalog keys fail explicitly.

The catalog is a bounded starting scenario: eight planets, Pluto and Earth's Moon. `DwarfPlanet` is a distinct display/catalog classification; it does not make Pluto a normal planet for homeworld selection or environmental guarantees. The catalog does not claim complete moon/asteroid coverage, precision orbital dynamics or measured terrain. Orbital spacing remains a presentation scale. No other race receives a disguised copy of Earth.

## Physical references and model conventions

Rounded planet radius/mass ratios and broad temperature/gravity references use [NASA GSFC's archival planetary tables](https://pwg.gsfc.nasa.gov/stargaze/SplanetsA.htm). Broad distinctions are consistent with [NASA's planet overview](https://science.nasa.gov/solar-system/planets/), [Venus facts](https://science.nasa.gov/venus/venus-facts/), [Mars facts](https://science.nasa.gov/mars/facts/) and [Moon facts](https://science.nasa.gov/moon/facts/). Pluto's classification and broad physical properties use [NASA's Pluto facts](https://science.nasa.gov/dwarf-planets/pluto/facts/); eccentricity and inclination use the [NASA NSSDC Pluto fact sheet](https://nssdc.gsfc.nasa.gov/planetary/factsheet/plutofact.html?level=1). The Pluto sources were reviewed on 2026-09-11. The tables are approximate references, not a precision ephemeris.

Earth has land, liquid water, an oxygen/nitrogen atmosphere and a terrestrial human habitat (`IsImmersedEnvironment = false`). Venus is hot, high-pressure and carbon-dioxide rich; Mercury and the Moon are airless in the broad atmosphere model. Jupiter, Saturn, Uranus and Neptune have no solid surface. The giant planets' 100 kPa pressure denotes a reference atmospheric layer, not a solid surface or colonizable ground. Scalar temperatures average or select representative conditions; they do not simulate day/night or depth profiles. Radiation hazard remains an explicit game-model parameter. Generic resource/anomaly/native flags are not invented for the solar planets.

## Save compatibility

Physical worlds are reconstructed from campaign seed plus saved star records. Preset-bearing standalone galaxy saves use format 10; preset-bearing campaign saves with Diplomacy use format 11. Older binaries reject these version numbers before interpreting the catalog. This prevents an older generator from silently replacing Earth and the other solar planets with procedural bodies under the same names or IDs.

Existing procedural saves remain procedural and are never converted by a display name. Current-format saves explicitly tagged `sol-v1` receive Pluto as an additive catalog upgrade only when the original Mercury-through-Neptune and Moon identities remain intact; their existing bodies, IDs, settlements and observations are preserved value-for-value. Loading does not rewrite the save file. The next normal save persists Pluto and its orbit fields using the existing format, so no format-number bump is required for these optional additive fields.

`KnownSystemExplorationView.CatalogPresetId` is exposed only after full survey. Reconnaissance cannot supply a material/canonical-surface key. The human home system follows the existing complete-home-survey rule. Presentation owns sourced artwork and uses the confirmed preset identity; illustration does not change authoritative habitat facts.

## Evidence

- Core runtime: 11/11 primary groups plus all module initializers pass.
- Simulation: 22/22 primary groups plus module initializers pass.
- Quality: 8/8 primary groups plus module initializers pass.
- Complete Species mechanics suite passes, including twelve-seed natural homeworld planning and legacy migrations.
- New Sol regression coverage checks six fresh campaign seeds, one human origin, distinct nonhuman founding homes, stable planet/moon IDs and order, physically sensible broad categories, preservation through diversity conditioning and display renaming, survey-confidence gating, and new/old save reconstruction.
- Guided demo reaches a legitimate second settlement after three science surveys in 166.57 active seconds at demo speed. All research, construction, ship population reservation and actual settlement checks remain active.
- `tests/fixtures/legacy-procedural-v8.json` was produced using the actual pre-Sol generation/persistence assembly from source at `94b0e63`, with seed 20260908, 24 systems, four pre-warp factions, one ancient faction and radius 450. It includes three legacy human-physiology factions, which remain unchanged. Its 204 reconstructed bodies retain SHA-256 `B720C6F22154F193B1F85E995E5AE52BF1B3707AA10504CEF3AAF65056CA66A9` over the compact default-serialized body array.
- The actual pre-Sol assembly was loaded in an isolated managed context. Its v8 galaxy reader rejected format 10, and its v9 campaign reader rejected format 11 with explicit unsupported-format diagnostics.

Checks use caught managed DLL hosts and actual repository sources; no scratch apphost executable is launched or committed. Response files and logs are kept outside the repository under workspace `work/celestial-graphics-checks/`. Real Godot input/screenshots and the combined sourced-texture rendering remain Core/Testing acceptance work.
