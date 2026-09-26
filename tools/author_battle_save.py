import json, copy, sys

d = json.load(open('native-tests/fixtures/player-campaign-json.json'))
row = [r for r in d['Rows'] if r.get('Name') == 'valid-current17'][0]
src = json.loads(row['InputJson'])
gal = src['Galaxy']
dip = src['Diplomacy']

tick = round(src['SimulationDays'] * 1000)
# Identified contact gives the war relationship its legitimate basis.
dip['Contacts'].append({
    "ObserverCivilizationId": 0, "ContactId": "vask-hostile-signal",
    "TargetCivilizationId": 3, "FirstObservedTick": tick - 8,
    "LastObservedTick": tick, "LastObservedSystemId": 1,
    "Awareness": 2, "Condition": 1, "CommunicationAvailable": False,
    "Confidence": 0.9})
# Hostility: player (0) vs Vask Dominion (3) at war.
dip['Relationships'].append({
    "CivilizationAId": 0, "CivilizationBId": 3, "PoliticalState": 3,
    "Trust": 0.0, "Hostility": 0.9, "Fear": 0.2, "Respect": 0.0,
    "Cooperation": 0.0,
    "Grievances": [{"CreatedAtTick": 12, "SourceCivilizationId": 3,
                    "Severity": 0.8, "Reason": "border incursion"}]})

# Third hostile formation stays unengaged: its cohort/vessel detail must remain
# hidden from the player observer (secrecy evidence). It binds an authored
# picket fleet via a matching warp_scout cohort.
picket = copy.deepcopy(gal['Fleets'][6])
picket['Id'] = 8
picket['Name'] = 'Vask Picket'
picket['CurrentSystemId'] = 1
gal['Fleets'].append(picket)

# The battle-art binder only draws the human patrol_corvette hull; fleet 0
# becomes the replay's owned corvette so exactly one sprite is bound. The
# design's role is Military, so the fleet role must match (reference
# validation rejects role-incompatible designs).
gal['Fleets'][0]['DesignId'] = 'patrol_corvette'
gal['Fleets'][0]['Role'] = 3

pt = lambda x, y: {"X": x, "Y": y, "Vector": {}, "IsFinite": True}
loadout = {"MassPerShip": 100, "Acceleration": 18, "MaximumSpeed": 120,
           "ShieldPerShip": 35, "ArmorPerShip": 45, "HullPerShip": 95,
           "ReactorOutputPerShip": 100, "CoolingPerShip": 28,
           "WarpStabilization": 50, "WarpSpoolSeconds": 12,
           "ModuleSlotCapacity": 12, "MaximumModuleMass": 420,
           "Weapons": [{"Id": "beam_battery", "Kind": 0, "MountsPerShip": 1,
                        "DamagePerShot": 28, "ShotsPerSecond": 0.16666667,
                        "Range": 700, "Accuracy": 0.65, "PowerPerSecond": 3,
                        "HeatPerSecond": 2}],
           "Modules": [{"Id": "reactor", "Kind": 0, "InstalledCount": 1,
                        "MassEach": 18, "PowerPerSecondEach": 0,
                        "HeatPerSecondEach": 0, "Condition": 1,
                        "Enabled": True, "EffectiveRange": 0,
                        "FieldStrength": 100, "DetectionSignature": 0,
                        "Slots": 1},
                       {"Id": "warp_drive", "Kind": 3, "InstalledCount": 1,
                        "MassEach": 22, "PowerPerSecondEach": 12,
                        "HeatPerSecondEach": 0, "Condition": 1,
                        "Enabled": True, "EffectiveRange": 0,
                        "FieldStrength": 0, "DetectionSignature": 0,
                        "Slots": 1}]}

def vessel(i, name, flagship=False):
    return {"Id": i, "Name": name, "DesignId": "warp_scout",
            "IsFlagship": flagship, "IsCarrier": False, "IsInterdictor": False,
            "IsStoryShip": False, "HullFraction": 1.0, "EngineFraction": 1.0,
            "SensorFraction": 1.0, "WarpDriveFraction": 1.0,
            "ReactorFraction": 1.0, "InterdictorFraction": 1.0,
            "BattlesFought": 0, "ConfirmedKills": 0, "Destroyed": False,
            "Escaped": False}

def formation(fid, civ, fleet_id, tf_id, name, x, y, hx, hy, shape,
              vessels):
    return {"Id": fid, "CivilizationId": civ, "FleetId": fleet_id,
            "TaskForceId": tf_id, "Name": name, "Position": pt(x, y),
            "Velocity": pt(6.0, 0.0), "Heading": pt(hx, hy),
            "Objective": pt(0, 0), "Shape": shape, "Order": 0,
            "TargetFormationId": None, "ProtectedFormationId": None,
            "InterdictorProtection": 1, "Cohesion": 1.0, "Morale": 1.0,
            "ShieldPool": 35.0 * len(vessels), "ArmorPool": 45.0 * len(vessels),
            "HullPool": 95.0 * len(vessels),
            "HullLossThresholdPerShip": 95.0, "Heat": 0.0,
            "PowerReserve": 1.0, "WarpSpoolProgress": 0.0,
            "WarpBlocked": False, "Escaped": False, "Surrendered": False,
            "InitialShipCount": len(vessels), "DestroyedShips": 0,
            "HullDamageRemainder": 0.0, "Loadout": copy.deepcopy(loadout),
            "Cohorts": [], "ImportantVessels": vessels}

gal['ActiveCombatEncounter'] = {
    "SystemId": 0, "StartedDay": src['SimulationDays'],
    "Battle": {
        "BattleId": "0a0b0c0d-0000-4011-8000-1234567890ab",
        "Seed": 4616471093031469151, "Tick": 0, "SimulatedSeconds": 0.0,
        "PendingSeconds": 0.0, "NextEventSequence": 1, "NextSalvoId": 1,
        "Formations": [
            # Fleet 0 is the player's patrol corvette: its tactical vessel uses
            # the native zero-fleet 2^32 mapping so the battle-art binder finds
            # exactly one owned hull. Pioneer One stays a colony_ship cohort —
            # unsupported designs keep their tactical markers.
            {**formation(1, 0, 0, 1, "Home Guard", -420.0, -120.0, 1.0, 0.0, 2,
                         [{**vessel(1 << 32, "Pathfinder Corvette", True),
                           "DesignId": "patrol_corvette"},
                          vessel(1, "Pioneer One")]),
             "InitialShipCount": 2},
            formation(2, 3, 6, 6, "Vask Vanguard", 420.0, 120.0, -1.0, 0.0, 0,
                      [vessel(6, "Vask Dominion Scout", True),
                       vessel(7, "Vask Dominion Pioneer")]),
            {**formation(3, 3, 8, 8, "Vask Picket Line", 140.0, -360.0,
                         -1.0, 0.0, 3, []),
             "Cohorts": [{"Id": 31, "DesignId": "warp_scout",
                          "InitialCount": 1, "ActiveCount": 1,
                          "Experience": 0.5}]},
        ],
        "Events": [], "ActiveSalvos": []},
    "Vessels": [{"FleetId": 0, "FormationId": 1},
                {"FleetId": 1, "FormationId": 1},
                {"FleetId": 6, "FormationId": 2},
                {"FleetId": 7, "FormationId": 2},
                {"FleetId": 8, "FormationId": 3}],
    "EngagedFormationPairs": [{"FirstFormationId": 1, "SecondFormationId": 2}],
    "LastObservedEventSequence": 0, "Reconciled": False}

out = sys.argv[1]
json.dump(src, open(out, 'w'), ensure_ascii=False)
print('authored', out)
