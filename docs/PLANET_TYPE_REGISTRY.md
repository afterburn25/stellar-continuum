# Planet type registry

All **16 base classes and 65 subclasses** resolve to one shared Core registry.
The generator, developer index and exported catalog read these same records.

The complete catalog is [planet-art/type-registry.json](planet-art/type-registry.json).
It includes every image ID and a lookup of filenames, hashes and rejection reasons.
In the game, open **Developer controls → PLANET INDEX**, select a subclass, and
choose **VIEW RULES**. **VIEW EXAMPLE** returns to the selected world's details.

| Tracked field | Meaning |
| --- | --- |
| Base class / subclass | Stable class ID, subclass ID and display name |
| Valid orbital zones | Allowed irradiation bands evaluated from the actual star's flux and the class albedo |
| Temperature rules | Intersection of class/subclass surface temperature ranges, plus required heat source |
| Atmosphere rules | Pressure interval, composition rule, molecular mass and retention threshold |
| Water allowed | Permission for visible surface liquid water; also requires stable temperature/pressure and available water solvent |
| Ice allowed | Permission for visible surface ice |
| Volcanism allowed | Permission for visible volcanic/emissive surfaces, including cryovolcanic subclasses where specified |
| Rarity / generation | Base-class percentage, subclass selection weight, within-class percentage and overall baseline percentage |
| Accepted image pool | Approved images owned by this exact class/subclass |
| Compatible image pool | Explicitly compatible approved images from another class; surface permissions still apply |
| Rejected image pool | Rejected IDs, linked to original classification, filename, hash, reason and duplicate target |

There are **665 accepted and 422 rejected images**, accounting for all **1,087**
audited files from the **47 supplied folders**. Primary pool ownership is unique;
compatible reuse is listed separately. The rejection history includes 17 exact
duplicates and 70 Earth-geography exclusions. Twelve older `cracked/shattered`
audit labels resolve to `cracked/blown-apart`, retaining the original audited label.
Rejected records are metadata only: their image pixels are never runtime inputs.

## How the rules work

The five orbital bands use equilibrium temperature: deep cold below 120 K,
cold 120–240 K, temperate irradiation 240–350 K, warm 350–700 K, and hot
700–1,000,000 K. Lower limits are inclusive and upper limits exclusive.
These are broad eligibility bands, not fixed AU distances or a substitute for
surface climate. Greenhouse pressure, internal heat, periapsis heating, mass,
radius, atmosphere retention and available solvent still constrain generation.
An allowed cold zone does not guarantee that a warm surface can exist there.

Class baseline percentages total 100%. Subclass percentages total 100% within
each class. Temperate groups preserve their existing nested weights; for example,
Gaia's 5% share of the 4% Temperate baseline is 0.2% overall before eligibility.
Actual generated frequencies depend on the star and physical conditions. The
generator filters impossible choices, applies its physical modifiers and
renormalizes the remaining weights. Percentages are not promised galaxy counts.

Definitions live in `data/planets/planet-types-v1.json`. Accepted and rejected
artwork metadata live in `data/planets/planet-art-v1.json`. Do not maintain
separate editable image lists per UI or generator: the resolved pools are indexed
from these normalized sources. Configuration is compiled into Core, so changing
definitions requires rebuilding; the report is generated from the compiled data
with `stellar_planet_type_registry output.json`.

Existing saved worlds retain their stored appearance and physical environment.
Legacy bodies outside the current generation rules use the existing procedural
fallback. Sol's measured identities remain a separate explicit path. No new
per-body save fields or independent UI simulation are introduced.

## Validation

Startup validation rejects missing/unknown zones, invalid atmosphere rules,
contradictory surface permissions, invalid weights, overlapping pools, duplicate
approved identities/materials and rejection records without reasons. The export
path also blocks rejected status and overlapping pools. Rejected images cannot
be restored as approved saved appearances.

Regression coverage checks all 65 examples, percentage totals, all image ownership,
2,400 seeded physical cases, repeatability, persistence, developer controls at
720p/1080p/2160p and export tamper/rejection handling.

**ENGINE CAPABILITIES ADDED / EXTENDED:** authoritative resolved planet registry,
explicit subclass permissions and orbital bands, configurable atmosphere and
subclass weights, rejection provenance, shared developer rules view and catalog export.

**ENGINE LIMITATIONS REMAINING:** orbital bands and climate/retention are bounded
game approximations, and reported percentages are baselines. Configuration changes
require rebuilding. Existing saves are preserved rather than regenerated.
