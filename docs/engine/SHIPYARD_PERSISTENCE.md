# Gate 081 shipyard persistence boundary

## Exact source

- `CampaignSaveService.cs` restore lines 943–1092 and capture/identity validation lines 1590–1719 are authoritative. The surrounding galaxy codec and historical fleet/colony conversion are excluded.
- `ShipyardState.cs` supplies the owned runtime records, queue limit 8, order-ID length 128, canonical sequence grammar, and population-preservation guard.
- `ShipDesignRegistry` and `SpeciesCatalog` are immutable content dependencies. Native uses the already maintained ship design and species catalogs rather than copying IDs into the persistence layer.

## Public boundary

`shipyard_persistence.hpp` exposes owned `ShipyardPersistenceDto` and `QueuedShipBuildPersistenceDto`, exact restore and capture functions, and distinct restore-data/save-operation error categories. Restore borrows the authoritative civilization span only for pre-format-8 population-species fallback. Returned states and captured DTOs own every string and collection.

## Required ordering evidence

Restore first validates scalar accounting and every queued record, then active order identity, duplicate identities, active design sanitation/accounting/material bounds, constructs the state and synthesizes a legacy active ID, and finally walks the raw queue. Invalid zero-asset metadata may be discarded. Population, authorization, or order identity changes that into a hard failure. The accepted-entry counter, rather than raw source index, controls synthesized legacy queue IDs and the capacity-8 boundary. Species is resolved only for positive reserved population and uses civilization species before format 8 versus the stored species from format 8 onward. Canonical sequences are validated after queue materialization.

Capture validates runtime accounting before reading the guarded queue, calculates logical capacity from a known active design, distinguishes serializer raw-index overflow from accepted-entry overflow, rejects any discarded recoverable asset, synthesizes persisted IDs in accepted order, and validates canonical sequence monotonicity before returning detached DTOs. It preserves input order and does not mutate the source state.

The actual-source oracle invokes the private source helpers through reflection and unwraps `TargetInvocationException` precisely. Its retained fixture contains 48 rows: 47 replayable native rows and one explicit source-only boundary for a null reference element in `QueuedBuilds`. Native public vectors contain typed values, so a future parser must reject that null before calling production. Rows carry complete DTO/state/civilization inputs, pre/post fingerprints, owned results, typed errors, save-format version, and exact source hashes. Coverage includes legacy/current species, negative legacy population sanitation, unknown design drop versus asset/refund failure, active and queued material accounting, raw-index and accepted-slot overflow, legacy ID collisions, canonical/noncanonical IDs, sequence boundaries, null queue, nonfinite values, source number formatting, source input immutability, and detached capture independence.
