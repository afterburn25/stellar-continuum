"""Author a save that satisfies --settlement-smoke / --settlement-reload-smoke.

The settlement smokes require a player-owned, populated colony or outpost
vessel that is idle and orderable, plus a fully surveyed, uncolonized,
viable target body admitted to the player observer. Ordinary campaign
saves rarely have both, so this script grafts the preconditions onto a
copy of an existing save without touching production simulation code:

- the chosen vessel is made idle (transit fields cleared, route emptied);
- the player knowledge record gains full survey of the target system.

Usage:
    python tools/author_settlement_save.py --in save.json --out save.json
        [--fleet-id 1] [--system-id 18]

Defaults match the fleet smoke fixture: fleet 1 ("Pioneer One") retargeted
at system 18, which contains the viable uncolonized body 18002 (Elara).
Run the ordered smoke first; its autosave leaves an active settlement
mission, so the same output file then also satisfies
--settlement-reload-smoke.
"""

import argparse
import json


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--in", dest="src", required=True, help="source save JSON")
    parser.add_argument("--out", dest="dst", required=True, help="output save JSON")
    parser.add_argument("--fleet-id", type=int, default=1)
    parser.add_argument("--system-id", type=int, default=18)
    args = parser.parse_args()

    doc = json.load(open(args.src, encoding="utf-8"))
    galaxy = doc["Galaxy"]

    vessel = next(
        f for f in galaxy["Fleets"]
        if f["Id"] == args.fleet_id and f["CivilizationId"] == 0)
    if not vessel.get("EmbarkedPopulationMillions"):
        raise SystemExit(f"fleet {args.fleet_id} carries no embarked population")

    # Park the vessel so the settlement controller accepts a new order.
    vessel["TransitPhase"] = 0
    for key in ("DestinationSystemId", "TransitOriginSystemId",
                "TransitTargetSystemId", "DestinationPlanetaryBodyId",
                "SettlementBodyId", "ReconnaissanceSystemId",
                "FreightTargetOutpostId"):
        vessel[key] = None
    vessel["TransitProgress"] = 0.0
    for key in ("LocalTransitStartX", "LocalTransitStartY",
                "LocalTransitPositionX", "LocalTransitPositionY",
                "LocalTransitTargetX", "LocalTransitTargetY"):
        vessel[key] = 0.0
    vessel["PlannedRouteSystemIds"] = []
    vessel["SettlementDaysCompleted"] = 0.0

    # Grant the observer full survey knowledge of the target system so the
    # exact-target preview path is admitted.
    knowledge = next(
        k for k in galaxy["Knowledge"] if k["CivilizationId"] == 0)
    known = set(knowledge["KnownSystemIds"])
    known.add(args.system_id)
    knowledge["KnownSystemIds"] = sorted(known)
    surveys = knowledge.setdefault("SystemSurveys", [])
    entry = next(
        (s for s in surveys if s["SystemId"] == args.system_id), None)
    if entry is None:
        surveys.append({"SystemId": args.system_id, "Level": 3,
                        "Progress": 1.0})
    else:
        entry["Level"] = 3
        entry["Progress"] = 1.0

    json.dump(doc, open(args.dst, "w", encoding="utf-8"))
    print(f"authored {args.dst}: fleet {args.fleet_id} idle, "
          f"system {args.system_id} fully surveyed")


if __name__ == "__main__":
    main()
