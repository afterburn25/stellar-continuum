# System sky profiles

SystemSkyProfile is a small derived value object. It combines campaign/system identity with observer-known primary, secondary and tertiary stellar classes, archetype and broad galactic position where a core/radius is available. It does not create a Node per galaxy system.

## Inputs and limitations

The saved model supplies stellar classes and positions, but not stellar luminosity, orbital distance in astronomical units, satellite separations in kilometres, rotation periods, rings, precise eclipses, galactic gas tomography or magnetic fields. V1 uses restrained, deterministic illustration conventions for those missing quantities. Inspector reasons and this document distinguish them from measured facts.

Actual known companion membership is preserved. A single star never acquires an invented binary companion. Primary colors range from warm red/orange through neutral white to cool blue-white. The same light profile reaches orbit materials, the surface directional light, ambient scattering and companion lights. A black hole does not receive a bright stellar photosphere in the new surface sky.

Sky star instances are bounded between 480 and 1,600 in the local 3D view. An outer observed radius receives a sparse field, an inner radius a denser field. When galaxy core geometry is unavailable, the profile states that position context is unavailable. It does not infer a precise spiral-arm membership from a random seed.

## Space and surface

One original procedural sky shader produces restrained nebula filaments, dust and background points; the existing 2D backdrop consumes the same profile colors and density. Nebula archetypes and protostars produce stronger cloud context. Ordinary systems mostly remain dark. Bright nebula wallpaper is not applied to every system.

Surface scattering uses class chemistry, pressure-derived density and primary light color. Fog affects distant terrain while preserving sky gradients and visible stellar discs. Vacuum and pressure below 0.1 kPa disable scattering, clouds and fog. Airless terrain receives low ambient fill and sharp primary-light shadows. The orbital descent fades the same atmosphere with altitude.

Known binary and triple stars have separate sky discs and directional illumination. Their screen separations and apparent sizes are bounded scenic conventions, not a simulated ephemeris. EclipsePotential, AuroraPotential and DebrisBand are descriptors for future physically richer effects; they do not currently run a time-dependent eclipse or magnetosphere simulation.

## Moon skies and rings

Surface companions come from the observer-safe system markers. Planet surfaces can show known moons. Moon surfaces can show their known parent planet, with the parent's actual visual identity. There are at most six companion meshes and three stellar lights, and stale companions are removed when the body changes.

Earth's Moon retains the existing lunar map and half-degree illustration. Generic moons have restrained bounded apparent sizes. Parent giants are larger, with full lit globes and ring geometry where their visual profile is ringed. A companion below the local illustrated horizon is hidden. Parent membership and known identity are factual; positions and angular sizes are schematic because orbital order is the only available separation data.

Saturn retains its authored ring presentation. A small deterministic subset of noncanonical giant variants receives decorative rings, clearly a visual modifier; no unmodeled ring mining resource is invented. V1 does not claim physically correct ring-plane eclipses or orbital conjunction timing.

## Resource ownership

Only the currently visible scenes create meshes/materials. Original shaders and the packed detail master are shared resources. No background raster is stored per system, and no 4K image is created per planet. The 2D material cache has a 128-entry bound. Approved raster maps are loaded only when a surveyed visible variant requests an existing path.

The gallery repeatedly switches all classes and celestial contexts while recording object/node counts and process/video memory. Resource cleanup is part of the capture's success criteria. See PLANET_VISUAL_VALIDATION.md for measured local results and limitations.
