# Species environment and natural-home migration

Next slice after native physical catalogs at dc289005: port the existing species-relative environmental interpretation and normal natural-home planner. No new species, biology, balance, homeworld guarantees or gameplay rules.

Frozen interface: `core/include/stellar/core/species_environment.hpp`. Species identity remains the four existing string IDs. Environmental profiles project only the fields consumed by the current evaluator; demographic, morphological, metabolic and adaptation-progression systems remain in C#. This projection must not be serialized as a complete species definition. Atmosphere/solvent enum ordering differs between planetary facts and species interpretation; explicit mapping is required.

The evaluator preserves tolerance bands, per-axis scores, limiting-factor tie order, geometric operational capacity, mitigation flags, adaptation identity/validation, and settlement thresholds (0.20 naturally viable, 0.95 comfortable). Environmental facts remain immutable. Fresh human factions remain on Earth's exact catalog body; nonhuman factions cannot consume Sol. AI personality is independent of species identity. Normal home selection preserves constrained-species ordering, distance metric, scoring and ID tie-breaks.

`plan_species_homeworlds` is the normal planner, not the nearby-expansion constrained fallback or completed civilization seeder. The command-line preview must state this boundary. Full campaign generation, the fallback, founding leaders/colonies/economies and save-v16 remain later gates. C# source-generated oracle cases must cover all species, environmental boundaries, adaptation, canonical/legacy seed assignment, natural-home outcomes and clean failures.

Ownership: environment implementation owns `core/src/species_environment.cpp`; home planner/assignment owns `core/src/species_homeworlds.cpp`; reference fixture generation owns `tests/Stellar.Species.ParityGenerator`; coordinator owns shared header, CMake, native consumer tests and application/export integration. Preserve Engine/Core separation.
