# Station Outfitting

Stellar Continuum human station design — 2026-09-13.

Customize each station as a ship-style loadout: choose weapons, shield generators,
armor and service modules. Two limits apply together: **allocation points** and
**compatible physical slots**. These are proposed design rules and an allocation
preview, not a playable engine feature.

## Core tier capacity

All figures below are initial balancing candidates. Allocation points are a
capacity ceiling, not a currency; unspent points are allowed. Every tier increases
both total points and total equipment slots.

| Tier | Allocation points | Service bays | Light mounts | Medium mounts | Heavy mounts | Total equipment slots |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 20 | 4 | 4 | 0 | 0 | 8 |
| 2 | 40 | 8 | 8 | 4 | 0 | 20 |
| 3 | 65 | 12 | 12 | 4 | 2 | 30 |
| 4 | 100 | 16 | 16 | 8 | 4 | 44 |
| 5 | 150 | 24 | 24 | 12 | 6 | 66 |
| 6 | 210 | 32 | 32 | 16 | 8 | 88 |
| 7 | 280 | 40 | 40 | 20 | 10 | 110 |

Tiers 5 and 6 stack new decks above the Tier 4 base. Tier 7 encloses the retained
three elevations and adds bays and hardpoint frames to the new hull sections.
Earlier sockets and installed equipment stay in place. New capacity becomes
available only after the core upgrade is commissioned.

Built-in docking bays, clamps, personnel tunnels, basic command and core maintenance
systems are included in the core. They use **zero equipment slots and zero
allocation points**. A bare station starts with zero optional equipment and can
still dock compatible ships.

## Equipment choices and proposed costs

Each entry occupies one compatible slot. Its level determines its full allocation
cost; a Level 3 item pays the Level 3 cost, not the sum of all previous levels.
Research, minimum core tier, construction resources, power, heat, staffing and
clearance remain separate requirements.

| Equipment | Slot | Level 1 points | Level 2 points | Level 3 points |
| --- | --- | ---: | ---: | ---: |
| Point defense | Light | 2 | 4 | 6 |
| Defense turret | Medium | 4 | 7 | 10 |
| Heavy battery | Heavy | 8 | 12 | 18 |
| Shield generators | Service | 5 | 8 | 12 |
| Protection / armor | Service | 3 | 5 | 8 |
| Docking expansion / traffic | Service | 2 | 4 | 6 |
| Habitat / life support | Service | 2 | 3 | 4 |
| Power | Service | 2 | 3 | 5 |
| Thermal control | Service | 1 | 2 | 3 |
| Cargo / logistics | Service | 2 | 3 | 5 |
| Sensors / communications | Service | 2 | 4 | 6 |
| Research | Service | 4 | 6 | 9 |
| Shipyard / repair | Service | 5 | 8 | 12 |
| Industry / fabrication | Service | 4 | 7 | 10 |

Shield generators become a defined optional family with visible projectors and
emitters at three levels. Armor modules add visible protection assemblies. Higher
levels and different equipment produce distinct geometry. Final shield strength,
coverage, armor behavior and weapon statistics still need gameplay balancing.

For example, a proposed Tier 1 defense loadout can use one Level 1 shield (5),
one Level 1 armor module (3), one Level 1 power module (2), one Level 1 radiator
(1), and four Level 1 point-defense mounts (8). This uses **19 of 20 points and
all eight slots**. The spare point cannot create another slot. This illustrates
allocation only; actual power, heat, research and combat suitability remain untested.

## Outfitting and refits

- Choose a researched equipment family and level, then a compatible free slot.
- Show used / available points and used / available slots by class together.
- Block fitting when either capacity is exceeded; a heavy weapon cannot use a
  light mount just because points remain.
- A larger core does not bypass research. Shields, armor and every equipment
  level require their own completed unlock and supported core tier.
- Empty slots stay visible. The player can leave points and slots unused.
- Disabled or damaged equipment still occupies its slot and consumes points.
- Replacing an item in the same slot reserves the positive difference between
  target and current point cost. The socket remains reserved for that refit.
- Capacity from removal or a cheaper replacement returns after decommissioning
  or the replacement finishes. Cancelling work releases its unused reservation
  while retaining the old item. These are capacity changes, not promised refunds
  of construction materials or credits.
- Core upgrades retain existing loadouts and increase capacity only when complete.
- Save named loadout blueprints with core tier, catalog revision, socket IDs,
  equipment IDs and levels. Revalidate all requirements when applying one to
  another station; loading a blueprint queues valid construction, not instant gear.

## Preview and implementation

The in-conversation allocation preview lets the player choose a tier, equipment
and level, fit or remove equipment, and inspect remaining points and slot capacity.
Listed technology levels are assumed researched in this preview. It does not
simulate live research, construction, power, heat, combat or personnel.

The authoritative proposed numbers live in `HUMAN_STATION_DESIGN.json`. The
engine still needs an outfitting interface, actual station adapters, research
binding, paid refits, persistent loadouts and model attachment checks. Existing
ship combat loadout data is a reference, not a completed station customizer.

## Required verification

Check every tier raises point and slot ceilings; all equipment levels have
positive costs; Tier 7 preserves earlier sockets while adding new ones. Check
research, allocation and physical slot requirements independently. Check reserved
refits cannot overbook capacity, damaged modules retain their cost, removal does
not release capacity early, built-in docks remain free of equipment allocation,
and saved blueprints cannot bypass current ownership, research or capacity rules.

