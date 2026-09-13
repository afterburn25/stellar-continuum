# Fleet travel validation

## Completed 0.1.11 travel libraries

The uncommitted 0.1.11 travel slice contains local transit, lane-network construction and routing, and operational reach/route orders. It is a library checkpoint: it does not advance a full campaign, make the native runtime playable, claim FPS performance, or complete time integration.

| Gate | Actual C# cases | Fixture SHA-256 | Contract |
| --- | ---: | --- | --- |
| Local transit | 78 | `27E6D49701D5FA6C3FA399044B074C1061B9B632509B75C75E92A2C384A70E9D` | [FLEET_TRANSIT_MIGRATION_CONTRACT.md](FLEET_TRANSIT_MIGRATION_CONTRACT.md) |
| Lane network | 53 | `47E78469012FA3A0E13529D59B8FED581EBB425E0A985541DD34FF56B1540068` | [LANE_NETWORK_MIGRATION_CONTRACT.md](LANE_NETWORK_MIGRATION_CONTRACT.md) |
| Operational reach | 89 | `18FE8EF3C03F8D339A5030D358466F6A9FFE8C091AC2BEE71D576AF74CCE0C43` | [OPERATIONAL_REACH_MIGRATION_CONTRACT.md](OPERATIONAL_REACH_MIGRATION_CONTRACT.md) |

`work/native-021-reach-testing.log` records 27/27 CTest and 16/16 Python checks. Lane-network parity includes the reviewed 2,500-node connectivity/cache smoke; it passed in about 2.55 seconds in that testing run. The contracts retain the reviewed cache ownership, topology ordering, exact-range, route, fuel/refueling, local-transit, and native identity-exhaustion boundaries.

## Pre-commit package evidence

Release benchmark: `Builds/Windows/StellarContinuum-windows-benchmark-d4e74593-20260913T034052755002Z`.

Debug development: `Builds/Windows/StellarContinuum-windows-development-d4e74593-20260913T034415026455Z`.

Both are pre-commit `sourceDirty: true` packages at source `d4e745938a94ea98d5386ea02243e999451fa548`; both report engine 0.1.11 and passed 27/27 CTest plus 16/16 Python checks. They are not a clean 0.1.11 checkpoint.

## Next dependencies

Knowledge, civilian recovery, and fresh campaign initialization come before full travel/time integration. See [CIVILIAN_RECOVERY_MIGRATION_CONTRACT.md](CIVILIAN_RECOVERY_MIGRATION_CONTRACT.md) and [FRESH_CAMPAIGN_MIGRATION_CONTRACT.md](FRESH_CAMPAIGN_MIGRATION_CONTRACT.md).

## 0.1.12 fresh-campaign evidence

The reviewed pre-commit benchmark is `Builds/Windows/StellarContinuum-windows-benchmark-4763cba2-20260913T043305145225Z`; Debug development is `Builds/Windows/StellarContinuum-windows-development-4763cba2-20260913T043441991554Z`. Both record engine 0.1.12, source `4763cba2810e5c109d5834a26ea0072b5fd6bf32`, and `sourceDirty: true`; each passed 30/30 CTest and 19/19 Python checks. Fresh campaign has nine parity cases, including fallback coverage; the fallback remains a diagnostic limitation, not a full campaign simulation. Relocated validation reports `relocatedFreshCampaign: true`.
