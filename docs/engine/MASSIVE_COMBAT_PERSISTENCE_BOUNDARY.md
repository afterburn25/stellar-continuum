# Gate 079 dependency and size assessment

## Exact bounded source

- `MassiveCombatState.cs`: 211 lines, SHA-256 `7F6607AF41B06464FE06D378FA44F43066F6D97DF11387EA3AC415070DBF0BD5`.
- `MassiveCombatContracts.cs`: 155 lines, SHA-256 `14F1C436959E170F8FA0B3802D71299C14D9950B138D49E6C049EE1A23F47685`; only persisted enums, point and event are needed.
- `CampaignMassiveCombat.cs`: 400 lines, SHA-256 `61CA23E8B61E4D167E1137F7EBFFE4748B9292C8B2CA01E643CB683E3551E05A`; Gate 079 uses only `CampaignMassiveEncounter`, binding/engagement records and its `Validate` method (lines 10-64).
- `CampaignSaveService.cs`: 2,045 lines, SHA-256 `5D3B16323EDEAD7E46A73C903161ACAF2DCF1CE104046D08A739FE8FBC26599E`; Gate 079 uses only encounter/battle/formation/loadout/vessel clone projection (lines 133-203) and the later call to encounter validation. The galaxy codec remains outside this gate.

The owned graph adds 4 tactical enums, a 2-float point, event (10 persisted fields), salvo (8), cohort (5), formation (34 scalar/optional/value fields plus 3 owned child collections), battle (10 persisted fields), binding (2), engagement (2), and encounter (7). Existing `MassiveCombatLoadout`, weapons/modules, `MassiveVesselState`, `FleetState`, and `StellarSystem` are reused. Battle metrics and computed observer DTOs are not persisted and are excluded.

Worst authored collection bounds are 4,096 formations, 200,000 initial ships/bindings, 128 cohorts and 256 important vessels per formation, 256 events, 256 active salvos and 65,536 engagement pairs. Native validation therefore needs checked integer accumulation and indexed identity sets; linear nested membership scans would be inappropriate at the source limits.

## Proposed staged port

1. Add the owned tactical DTOs and exact enum ordinals. Represent `Guid` as the exact 16 persisted bytes; JSON spelling belongs to the later codec.
2. Port battle/formation/cohort/salvo validation in source order. `Validate` is deliberately non-const because source materializes `InitialShipCount` when it is nonpositive. Translate only known existing loadout/vessel validation failures into the source operation category.
3. Port encounter validation against borrowed authoritative system/fleet spans: battle first, encounter limits, source `ToDictionary` duplicate-fleet category, binding identity/design/profile checks, exact initial-ship conservation, then canonical engagement-pair checks.
4. Add explicit deep-clone entry points matching `CampaignSaveService` capture and prove source and clone independence after every nested collection mutation.
5. Retain an actual-source fixture with complete pre/post state, exact errors and mutation-on-failure boundaries. Include empty/nonempty Guid, finite/time checks, all enum bounds, Unicode blank names, duplicate/global identities, child/loadout order, legacy initial-count materialization, checked-sum overflow, limits, event/salvo counters, duplicate galaxy fleets, bindings/design fallback, conservation and engagement ordering.

A later gate may add tactical create/advance/order/observer/aftermath behavior and deterministic continuation. Gate 079 does not add a campaign save codec, attach the encounter to `FreshCampaignState`, advance battle time, or claim player-save compatibility.
