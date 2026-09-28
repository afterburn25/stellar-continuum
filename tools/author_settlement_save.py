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
    python tools/author_settlement_save.py --in save.json --out save.json
        --travel --known-neighbor 17 [--relocate-to 11]

Defaults match the fleet smoke fixture: fleet 1 ("Pioneer One") retargeted
at system 18, which contains the viable uncolonized body 18002 (Elara).
Run the ordered smoke first; its autosave leaves an active settlement
mission, so the same output file then also satisfies
--settlement-reload-smoke.

--travel instead authors a --system-travel-smoke fixture: the transiting
vessel is left untouched, one lane-connected neighbor gains partial
survey (so it carries a known_label while others stay "????"), and other
idle player fleets are relocated out of the home system so a single click
on the anchored marker selects the transiting vessel unambiguously.
"""

import argparse
import glob
import json
import os

_OTHER_SYSTEM_POSITION = (-154.1, 166.9)  # fleet fixture's system 11


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--in", dest="src", required=True, help="source save JSON")
    parser.add_argument("--out", dest="dst", required=True, help="output save JSON")
    parser.add_argument("--fleet-id", type=int, default=1)
    parser.add_argument("--system-id", type=int, default=18)
    parser.add_argument("--travel", action="store_true",
                        help="author a --system-travel-smoke fixture")
    parser.add_argument("--known-neighbor", type=int, default=17,
                        help="lane-connected neighbor to partially survey")
    parser.add_argument("--relocate-to", type=int, default=11,
                        help="system that receives co-located idle fleets")
    args = parser.parse_args()

    doc = json.load(open(args.src, encoding="utf-8"))
    galaxy = doc["Galaxy"]

    vessel = next(
        f for f in galaxy["Fleets"]
        if f["Id"] == args.fleet_id and f["CivilizationId"] == 0)

    if args.travel:
        # Keep the vessel's local transit; give it one known neighbor and
        # clear co-located idle fleets so the marker click is unambiguous.
        home = vessel["CurrentSystemId"]
        known = next(
            k for k in galaxy["Knowledge"] if k["CivilizationId"] == 0)
        if args.known_neighbor not in known["KnownSystemIds"]:
            known["KnownSystemIds"].append(args.known_neighbor)
            known["KnownSystemIds"].sort()
        surveys = known.setdefault("SystemSurveys", [])
        if not any(s["SystemId"] == args.known_neighbor for s in surveys):
            surveys.append({"SystemId": args.known_neighbor, "Level": 2,
                            "Progress": 1.0})
        for other in galaxy["Fleets"]:
            if (other["CivilizationId"] == 0 and other["Id"] != args.fleet_id
                    and other["CurrentSystemId"] == home):
                other["CurrentSystemId"] = args.relocate_to
                other["X"], other["Y"] = _OTHER_SYSTEM_POSITION
        json.dump(doc, open(args.dst, "w", encoding="utf-8"))
        print(f"authored {args.dst}: travel fixture, neighbor "
              f"{args.known_neighbor} partially surveyed")
        return

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
    # Hand-authored output is not engine-written: drop stale save sidecars
    # (.integrity FNV-1a64 checksum + rolling .bak history) or the loader
    # treats the fixture as corrupt and silently recovers the previous
    # autosave instead of the authored state.
    for sidecar in glob.glob(args.dst + ".bak*") + glob.glob(
            args.dst + ".integrity*"):
        os.remove(sidecar)
    print(f"authored {args.dst}: fleet {args.fleet_id} idle, "
          f"system {args.system_id} fully surveyed")


if __name__ == "__main__":
    main()
