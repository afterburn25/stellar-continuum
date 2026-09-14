// Canonical biology and role payloads are read from the game registries.
// Names, dimensions, clearance allowances and fitting budgets are content proposals.
export const races = [
  {
    id: "pelagic_high_pressure",
    short: "Pelagic",
    title: "Pelagic High-Pressure",
    tagline: "Pressure shells • flowing ribs • immersed habitats",
    color: "#55dccd",
    hull: "#94b5b4",
    dark: "#1b3c49",
    metal: "#487b86",
    glow: "#4ffff0",
    accent: "#e3d49a",
    clearance: "3.2 m swimways · 2.8 m turning volume",
    biology:
      "Radial aquatic bodies, 2.10 × 0.80 m; 0.85 g, 282 K, 350 kPa; continuous immersion.",
    habitatM3: 90,
    shapeAllowance: 0.12,
    names: [
      "Tidefinder",
      "Songweaver",
      "Reefguard",
      "Tidehold",
      "Shoalforge",
      "Great Current",
    ],
    sizes: [
      [155, 94, 54],
      [260, 170, 95],
      [210, 142, 72],
      [520, 280, 178],
      [4200, 2400, 1500],
      [12000, 8000, 3600],
    ],
    language:
      "Rounded pressure vessels, four-way access, water circulation trunks and a sweeping protective shell.",
  },
  {
    id: "compact_high_gravity",
    short: "High-Gravity",
    title: "Compact High-Gravity",
    tagline: "Stepped armor • wide stance • short structural spans",
    color: "#ffba67",
    hull: "#625d57",
    dark: "#282c33",
    metal: "#988373",
    glow: "#ffc167",
    accent: "#c37336",
    clearance: "2.0 m clear height · 2.6 m turning width",
    biology:
      "Horizontal quadrupedal bodies, 1.35 × 0.75 m; 1.75 g, 300 K, 160 kPa; short reach.",
    habitatM3: 55,
    shapeAllowance: 0.18,
    names: [
      "Cairn",
      "Deep Anvil",
      "Bulwark",
      "Loadstone",
      "Foundry Seed",
      "Mountainhome",
    ],
    sizes: [
      [116, 96, 36],
      [185, 168, 52],
      [168, 156, 49],
      [390, 320, 102],
      [3200, 2100, 1000],
      [9500, 6500, 2400],
    ],
    language:
      "Low broad decks, layered wedge armor, exposed transverse braces and paired heavy engine blocks.",
  },
  {
    id: "cryogenic_hydrocarbon",
    short: "Cryogenic",
    title: "Cryogenic Hydrocarbon",
    tagline: "Cold lanterns • thermal separation • sixfold structure",
    color: "#b4acff",
    hull: "#b9c6d3",
    dark: "#283548",
    metal: "#657691",
    glow: "#8caaff",
    accent: "#bc98ef",
    clearance: "2.4 m radial bays · 2.8 m turning diameter",
    biology:
      "Radial multipedal bodies, 1.20 × 1.00 m; 0.14 g, 94 K, 150 kPa; hydrocarbon solvent.",
    habitatM3: 65,
    shapeAllowance: 0.1,
    names: [
      "Pale Thread",
      "Aurora Lens",
      "Frost Lance",
      "Cold Vault",
      "Rime Seed",
      "Long Winter",
    ],
    sizes: [
      [184, 98, 78],
      [330, 224, 150],
      [248, 142, 110],
      [650, 380, 248],
      [4500, 2600, 1700],
      [13000, 7600, 4200],
    ],
    language:
      "Faceted cold habitat lanterns, sixfold docking and sensor geometry, long thermally isolated engine booms.",
  },
];
export const roles = [
  {
    id: "warp_scout",
    name: "Scout",
    crew: 24,
    passengers: 0,
    points: 14,
    power: 18,
    weapon: 2,
    utility: 2,
    defense: 1,
    cargo: 0,
    thermal: 1,
  },
  {
    id: "science_vessel",
    name: "Science vessel",
    crew: 72,
    passengers: 0,
    points: 24,
    power: 32,
    weapon: 2,
    utility: 4,
    defense: 2,
    cargo: 0,
    thermal: 2,
  },
  {
    id: "patrol_corvette",
    name: "Patrol corvette",
    crew: 85,
    passengers: 0,
    points: 36,
    power: 44,
    weapon: 6,
    utility: 2,
    defense: 2,
    cargo: 0,
    thermal: 2,
  },
  {
    id: "bulk_freighter",
    name: "Bulk freighter",
    crew: 60,
    passengers: 0,
    points: 26,
    power: 30,
    weapon: 2,
    utility: 2,
    defense: 2,
    cargo: 4,
    thermal: 2,
  },
  {
    id: "resource_outpost_ship",
    name: "Outpost carrier",
    crew: 180,
    passengers: 8000000,
    points: 42,
    power: 50,
    weapon: 4,
    utility: 4,
    defense: 4,
    cargo: 4,
    thermal: 2,
  },
  {
    id: "colony_ship",
    name: "Colony ark",
    crew: 320,
    passengers: 250000000,
    points: 56,
    power: 70,
    weapon: 6,
    utility: 4,
    defense: 4,
    cargo: 6,
    thermal: 2,
  },
];
export const moduleTypes = [
  {
    id: "beam_turret",
    name: "Beam turret",
    kind: "weapon",
    size: "M",
    points: 4,
    power: 6,
    description: "Twin energy barrels on a rotating armored mount.",
  },
  {
    id: "kinetic_turret",
    name: "Kinetic turret",
    kind: "weapon",
    size: "M",
    points: 4,
    power: 3,
    description: "Paired long rail barrels and an ammunition housing.",
  },
  {
    id: "missile_pod",
    name: "Missile battery",
    kind: "weapon",
    size: "L",
    points: 6,
    power: 2,
    description: "Six individually visible launch cells.",
  },
  {
    id: "point_defense",
    name: "Point defense",
    kind: "weapon",
    size: "S",
    points: 2,
    power: 2,
    description: "Compact four-barrel close defense mount.",
  },
  {
    id: "sensor_array",
    name: "Sensor array",
    kind: "utility",
    size: "M",
    points: 3,
    power: 3,
    description: "Raised array with a distinct dish or radial sensor head.",
  },
  {
    id: "command_relay",
    name: "Command relay",
    kind: "utility",
    size: "S",
    points: 2,
    power: 2,
    description: "Communications mast with visible transmitter vanes.",
  },
  {
    id: "shield_emitter",
    name: "Shield emitter",
    kind: "defense",
    size: "M",
    points: 4,
    power: 7,
    description:
      "Raised emitter cage; optional equipment, not a permanent hull detail.",
  },
  {
    id: "armor_plate",
    name: "Armor plating",
    kind: "defense",
    size: "L",
    points: 3,
    power: 0,
    description: "Raised layered external armor panel.",
  },
  {
    id: "cargo_pod",
    name: "Cargo pod",
    kind: "cargo",
    size: "L",
    points: 3,
    power: 1,
    description:
      "External freight container with race-specific pressure or insulation shell.",
  },
  {
    id: "habitat_pod",
    name: "Habitat support pod",
    kind: "cargo",
    size: "L",
    points: 5,
    power: 4,
    description:
      "Supplementary life-support volume; does not alter the game population capacity.",
  },
  {
    id: "radiator",
    name: "Thermal radiator",
    kind: "thermal",
    size: "M",
    points: 2,
    power: 0,
    description: "Deployable panel geometry with heat transport piping.",
  },
  {
    id: "power_unit",
    name: "Auxiliary power unit",
    kind: "thermal",
    size: "M",
    points: 4,
    power: 0,
    powerSupply: 6,
    description: "External power pod; adds six workshop power units.",
  },
];
export const sizeMeters = { S: 6, M: 12, L: 24 };
export const sizeRank = { S: 1, M: 2, L: 3 };
export const ships = races.flatMap((r) =>
  roles.map((role, i) => ({
    id: `sc.ships.${r.id}.${role.id}.v1`,
    raceId: r.id,
    roleId: role.id,
    name: r.names[i],
    dimensions: {
      length: r.sizes[i][0],
      width: r.sizes[i][1],
      height: r.sizes[i][2],
    },
    crew: role.crew,
    populationReservation: role.passengers,
    points: role.points,
    power: role.power,
    stage: "library_candidate",
    gameIntegration: "pending",
  })),
);
export const getRace = (id) => races.find((x) => x.id === id);
export const getRole = (id) => roles.find((x) => x.id === id);
export const getShip = (race, role) =>
  ships.find((x) => x.raceId === race && x.roleId === role);
export const getModule = (id) => moduleTypes.find((x) => x.id === id);
export function compatible(socket, module) {
  return (
    !!module &&
    socket.kind === module.kind &&
    sizeRank[module.size] <= sizeRank[socket.size]
  );
}
export function fittingTotals(ship, fittings) {
  const mods = Object.values(fittings).map(getModule).filter(Boolean);
  return {
    points: mods.reduce((a, m) => a + m.points, 0),
    power: mods.reduce((a, m) => a + m.power, 0),
    powerBudget:
      ship.power + mods.reduce((a, m) => a + (m.powerSupply || 0), 0),
  };
}
export function checkFitting(ship, sockets, fittings) {
  const errors = [];
  for (const [id, m] of Object.entries(fittings)) {
    const s = sockets.find((x) => x.id === id);
    if (!s || !compatible(s, getModule(m)))
      errors.push(`Incompatible fitting: ${id} / ${m}`);
  }
  const t = fittingTotals(ship, fittings);
  if (t.points > ship.points) errors.push("Allocation budget exceeded");
  if (t.power > t.powerBudget) errors.push("Power budget exceeded");
  return { valid: errors.length === 0, errors, ...t };
}
export const formatLength = (m) =>
  m >= 1000 ? `${(m / 1000).toFixed(2).replace(/0$/, "")} km` : `${m} m`;
