import * as THREE from "three";
import {
  getRace,
  getRole,
  getModule,
  sizeMeters,
  compatible,
  checkFitting,
} from "./catalog.mjs";
const V = (x = 0, y = 0, z = 0) => new THREE.Vector3(x, y, z);
function materials(r) {
  const make = (color, metalness = 0.6, roughness = 0.4) =>
    new THREE.MeshStandardMaterial({ color, metalness, roughness });
  return {
    hull: make(r.hull, 0.48, 0.38),
    dark: make(r.dark, 0.7, 0.45),
    metal: make(r.metal, 0.8, 0.32),
    accent: make(r.accent, 0.65, 0.35),
    glow: new THREE.MeshStandardMaterial({
      color: r.glow,
      emissive: r.glow,
      emissiveIntensity: 1.8,
      metalness: 0.2,
      roughness: 0.3,
    }),
    glass: new THREE.MeshStandardMaterial({
      color: r.color,
      metalness: 0.78,
      roughness: 0.18,
    }),
    radiator: make("#263d53", 0.8, 0.4),
  };
}
function builder(root, p, lod) {
  let seq = 0;
  const seg = lod ? 8 : 20;
  function mesh(g, mat, name, pos = [0, 0, 0], rot = [0, 0, 0]) {
    const m = new THREE.Mesh(g, p[mat]);
    m.name = `${name}_${++seq}`;
    m.position.set(...pos);
    m.rotation.set(...rot);
    root.add(m);
    return m;
  }
  return {
    root,
    p,
    seg,
    mesh,
    box: (s, pos, mat = "hull", name = "panel", rot = [0, 0, 0]) =>
      mesh(new THREE.BoxGeometry(...s), mat, name, pos, rot),
    ell: (s, pos, mat = "hull", name = "pressure_shell") => {
      const m = mesh(
        new THREE.SphereGeometry(1, seg, Math.max(6, seg / 2)),
        mat,
        name,
        pos,
      );
      m.scale.set(...s);
      return m;
    },
    cyl: (
      rt,
      rb,
      h,
      pos,
      mat = "metal",
      name = "collar",
      rot = [Math.PI / 2, 0, 0],
      n = seg,
    ) => mesh(new THREE.CylinderGeometry(rt, rb, h, n), mat, name, pos, rot),
    tor: (r, t, pos, mat = "metal", name = "ring", rot = [0, 0, 0]) =>
      mesh(
        new THREE.TorusGeometry(r, t, lod ? 4 : 8, seg),
        mat,
        name,
        pos,
        rot,
      ),
    beam: (a, b, w, mat = "metal", name = "brace") => {
      const av = V(...a),
        bv = V(...b),
        d = bv.clone().sub(av);
      const m = mesh(
        new THREE.CylinderGeometry(w, w, d.length(), lod ? 5 : 8),
        mat,
        name,
        av.add(bv).multiplyScalar(0.5).toArray(),
      );
      m.quaternion.setFromUnitVectors(V(0, 1, 0), d.normalize());
      return m;
    },
    loft: (sections, mat = "hull", name = "faceted_hull") => {
      const pts = [];
      for (const [z, w, h, y = 0] of sections) {
        pts.push(
          [-w, y, z],
          [-w * 0.78, y + h, z],
          [w * 0.78, y + h, z],
          [w, y, z],
          [w * 0.72, y - h, z],
          [-w * 0.72, y - h, z],
        );
      }
      const verts = [],
        idx = [];
      pts.forEach((p) => verts.push(...p));
      for (let k = 0; k < sections.length - 1; k++)
        for (let j = 0; j < 6; j++) {
          let a = k * 6 + j,
            b = k * 6 + ((j + 1) % 6),
            c = (k + 1) * 6 + ((j + 1) % 6),
            d = (k + 1) * 6 + j;
          idx.push(a, b, d, b, c, d);
        }
      const front = verts.length / 3;
      verts.push(0, sections[0][3] || 0, sections[0][0]);
      const back = verts.length / 3;
      const last = sections.at(-1);
      verts.push(0, last[3] || 0, last[0]);
      for (let j = 0; j < 6; j++) {
        idx.push(front, (j + 1) % 6, j);
        let o = (sections.length - 1) * 6;
        idx.push(back, o + j, o + ((j + 1) % 6));
      }
      const g = new THREE.BufferGeometry();
      g.setAttribute("position", new THREE.Float32BufferAttribute(verts, 3));
      for (let n = 0; n < idx.length; n += 3) {
        const swap = idx[n + 1];
        idx[n + 1] = idx[n + 2];
        idx[n + 2] = swap;
      }
      g.setIndex(idx);
      g.computeVertexNormals();
      return mesh(g, mat, name);
    },
  };
}
function engine(b, x, y, z, r = 0.045) {
  b.cyl(r, r * 0.85, 0.12, [x, y, z], "dark", "engine_shroud");
  b.tor(r * 0.85, 0.009, [x, y, z + 0.061], "metal", "nozzle_rim");
  b.cyl(r * 0.66, r * 0.66, 0.003, [x, y, z + 0.064], "glow", "engine_core");
}
function docking(b, x, y, z) {
  b.cyl(0.036, 0.045, 0.028, [x, y, z], "metal", "integral_docking_collar", [
    0,
    0,
    Math.PI / 2,
  ]);
  b.tor(
    0.03,
    0.004,
    [x + (x < 0 ? -0.017 : 0.017), y, z],
    "glow",
    "docking_status",
    [0, Math.PI / 2, 0],
  );
}
function pelagic(b, role) {
  const i = [
    "warp_scout",
    "science_vessel",
    "patrol_corvette",
    "bulk_freighter",
    "resource_outpost_ship",
    "colony_ship",
  ].indexOf(role);
  b.ell([0.13, 0.11, 0.44], [0, 0, 0], "dark", "water_circulation_trunk");
  b.ell([0.235, 0.095, 0.37], [0, 0.018, -0.08], "hull", "dorsal_mantle");
  b.ell(
    [0.1, 0.072, 0.21],
    [0, 0.038, -0.34],
    "hull",
    "forward_pressure_habitat",
  );
  for (const side of [-1, 1]) {
    b.ell(
      [0.092, 0.072, 0.32],
      [side * 0.2, -0.018, 0.035],
      "hull",
      "lateral_pressure_vessel",
    );
    for (let k = 0; k < 7; k++) {
      const z = -0.25 + k * 0.082;
      b.tor(
        0.077,
        0.008,
        [side * 0.2, -0.018, z],
        "metal",
        "pressure_bulkhead",
      );
    }
    b.beam(
      [0, -0.02, -0.28],
      [side * 0.3, -0.025, 0.27],
      0.018,
      "metal",
      "swept_keel",
    );
    engine(b, side * 0.2, -0.02, 0.355, 0.052);
    docking(b, side * 0.295, -0.015, -0.06);
    for (let k = 0; k < 6; k++)
      b.ell(
        [0.008, 0.008, 0.023],
        [side * 0.143, 0.103, -0.22 + k * 0.07],
        "glow",
        "bioluminescent_navigation",
      );
  }
  b.ell(
    [0.075, 0.018, 0.09],
    [0, 0.115, -0.22],
    "glass",
    "command_sensory_crown",
  );
  b.beam([0, -0.09, -0.42], [0, -0.105, 0.32], 0.012, "accent", "ventral_keel");
  if (i === 1) {
    for (const s of [-1, 1]) {
      b.beam([s * 0.12, 0.03, -0.04], [s * 0.36, 0.025, -0.12], 0.016);
      b.ell(
        [0.095, 0.068, 0.17],
        [s * 0.34, 0.025, -0.17],
        "hull",
        "immersed_laboratory",
      );
      b.tor(0.07, 0.009, [s * 0.34, 0.025, -0.27], "accent", "lab_collar");
    }
    b.tor(0.105, 0.012, [0, 0.12, 0.13], "metal", "acoustic_analysis_ring", [
      Math.PI / 2,
      0,
      0,
    ]);
  }
  if (i === 2) {
    for (const s of [-1, 1]) {
      b.ell(
        [0.074, 0.058, 0.4],
        [s * 0.3, -0.008, -0.04],
        "dark",
        "outer_defense_keel",
      );
      b.ell(
        [0.07, 0.018, 0.3],
        [s * 0.3, 0.04, -0.075],
        "hull",
        "layered_keel_plate",
      );
      engine(b, s * 0.3, -0.01, 0.38, 0.04);
    }
    b.loft(
      [
        [-0.49, 0.015, 0.016, 0.06],
        [-0.22, 0.085, 0.022, 0.09],
        [0.25, 0.07, 0.025, 0.08],
      ],
      "accent",
      "reinforced_dorsal_ridge",
    );
  }
  if (i >= 3) {
    const rows = i === 3 ? 3 : i === 4 ? 4 : 6;
    for (const s of [-1, 1])
      for (let k = 0; k < rows; k++) {
        const z = -0.31 + k * (0.63 / (rows - 1));
        b.ell(
          [0.12, 0.088, 0.085],
          [s * 0.305, -0.04, z],
          "hull",
          i === 3 ? "sealed_freight_cell" : "water_habitat_cell",
        );
        b.tor(
          0.081,
          0.008,
          [s * 0.305, -0.04, z],
          "accent",
          "cell_pressure_band",
        );
        b.beam([s * 0.12, -0.05, z], [s * 0.31, -0.05, z], 0.018);
      }
  }
  if (i === 4) {
    b.cyl(
      0.17,
      0.2,
      0.13,
      [0, -0.14, 0.025],
      "dark",
      "deployment_caisson",
      [0, 0, 0],
      12,
    );
    b.tor(0.17, 0.015, [0, -0.21, 0.025], "accent", "outpost_separation_ring", [
      Math.PI / 2,
      0,
      0,
    ]);
  }
  if (i === 5) {
    for (const y of [-0.17, 0.18])
      for (const s of [-1, 1]) {
        b.ell(
          [0.13, 0.075, 0.38],
          [s * 0.17, y, 0],
          "hull",
          "ark_habitat_stratum",
        );
        for (let k = 0; k < 6; k++)
          b.tor(
            0.077,
            0.006,
            [s * 0.17, y, -0.26 + k * 0.1],
            "metal",
            "ark_pressure_partition",
          );
        b.beam([s * 0.17, y, -0.25], [s * 0.17, 0, -0.25], 0.025);
        b.beam([s * 0.17, y, 0.25], [s * 0.17, 0, 0.25], 0.025);
      }
    b.ell([0.16, 0.035, 0.13], [0, 0.25, -0.05], "glass", "ark_communal_dome");
  }
}
function compact(b, role) {
  const i = [
    "warp_scout",
    "science_vessel",
    "patrol_corvette",
    "bulk_freighter",
    "resource_outpost_ship",
    "colony_ship",
  ].indexOf(role);
  b.loft(
    [
      [-0.5, 0.05, 0.025],
      [-0.34, 0.27, 0.062],
      [0.13, 0.32, 0.075],
      [0.34, 0.24, 0.065],
    ],
    "dark",
    "load_bearing_wedge",
  );
  b.loft(
    [
      [-0.47, 0.025, 0.023, 0.034],
      [-0.27, 0.235, 0.035, 0.055],
      [0.08, 0.26, 0.043, 0.065],
      [0.26, 0.17, 0.032, 0.062],
    ],
    "hull",
    "stepped_main_plate",
  );
  b.loft(
    [
      [-0.3, 0.065, 0.02, 0.1],
      [-0.18, 0.13, 0.04, 0.105],
      [0.06, 0.11, 0.035, 0.105],
      [0.17, 0.055, 0.02, 0.09],
    ],
    "metal",
    "reinforced_command_deck",
  );
  for (const s of [-1, 1]) {
    b.box(
      [0.15, 0.13, 0.26],
      [s * 0.27, -0.004, 0.27],
      "hull",
      "armored_drive_block",
    );
    engine(b, s * 0.27, 0, 0.43, 0.055);
    for (let k = 0; k < 5; k++) {
      b.box(
        [0.012, 0.017, 0.3],
        [s * (0.1 + k * 0.033), 0.104, -0.022],
        "accent",
        "deck_load_rib",
      );
      b.box(
        [0.09, 0.008, 0.016],
        [s * 0.2, 0.105, -0.24 + k * 0.085],
        "metal",
        "deck_plate_seam",
      );
    }
    for (let k = 0; k < 3; k++)
      b.box(
        [0.13, 0.012, 0.045],
        [s * 0.273, 0.075, 0.18 + k * 0.065],
        "dark",
        "drive_grille",
      );
    b.box(
      [0.09, 0.006, 0.02],
      [s * 0.08, 0.152, -0.19],
      "glass",
      "horizontal_bridge_strip",
    );
    docking(b, s * 0.325, -0.01, -0.09);
    b.box(
      [0.012, 0.024, 0.14],
      [s * 0.288, 0.068, -0.19],
      "glow",
      "navigation_inlay",
    );
  }
  b.box([0.36, 0.028, 0.05], [0, -0.073, 0.04], "metal", "transverse_keel");
  if (i === 1) {
    for (const s of [-1, 1]) {
      b.box(
        [0.2, 0.075, 0.28],
        [s * 0.35, 0.02, -0.1],
        "hull",
        "broad_laboratory_wing",
      );
      b.box([0.16, 0.012, 0.22], [s * 0.35, 0.065, -0.1], "dark", "lab_roof");
      for (let k = 0; k < 4; k++)
        b.box(
          [0.14, 0.007, 0.008],
          [s * 0.35, 0.077, -0.18 + k * 0.055],
          "accent",
          "lab_roof_brace",
        );
    }
    b.cyl(
      0.075,
      0.1,
      0.035,
      [0, 0.17, 0.07],
      "metal",
      "analysis_crown",
      [0, 0, 0],
      8,
    );
  }
  if (i === 2) {
    for (const s of [-1, 1]) {
      b.loft(
        [
          [-0.45, 0.045, 0.025, 0.016],
          [-0.29, 0.09, 0.05, 0.02],
          [0.2, 0.09, 0.05, 0.015],
          [0.34, 0.065, 0.03, 0.01],
        ],
        "hull",
        "outboard_armor_sponson",
      ).position.x = s * 0.35;
      for (let k = 0; k < 4; k++)
        b.box(
          [0.13, 0.018, 0.075],
          [s * 0.35, 0.08, -0.25 + k * 0.135],
          "accent",
          "replaceable_integral_plate",
        );
    }
    b.box([0.26, 0.028, 0.28], [0, 0.14, 0.03], "hull", "central_citadel");
  }
  if (i >= 3) {
    const rows = i === 3 ? 4 : i === 4 ? 4 : 6;
    for (const s of [-1, 1])
      for (let k = 0; k < rows; k++) {
        const z = -0.31 + k * (0.66 / (rows - 1));
        b.box(
          [0.22, 0.1, 0.095],
          [s * 0.34, -0.015, z],
          "hull",
          i === 3 ? "freight_vault" : "habitat_block",
        );
        b.box(
          [0.225, 0.012, 0.025],
          [s * 0.34, 0.045, z],
          "accent",
          "container_crossbrace",
        );
        b.box(
          [0.16, 0.008, 0.012],
          [s * 0.34, 0.056, z - 0.025],
          "dark",
          "service_trench",
        );
      }
    for (const z of [-0.28, 0, 0.28])
      b.box(
        [0.85, 0.035, 0.038],
        [0, -0.084, z],
        "metal",
        "through_hull_load_member",
      );
  }
  if (i === 4) {
    b.box(
      [0.29, 0.13, 0.36],
      [0, -0.145, 0.02],
      "dark",
      "folded_outpost_foundation",
    );
    for (const s of [-1, 1])
      b.box(
        [0.1, 0.12, 0.33],
        [s * 0.1, -0.19, 0.02],
        "hull",
        "deployment_slab",
      );
  }
  if (i === 5) {
    for (const y of [0.15, -0.15]) {
      b.box([0.6, 0.1, 0.68], [0, y, 0.03], "hull", "ark_deck_continent");
      for (let k = 0; k < 8; k++) {
        b.box(
          [0.56, 0.007, 0.014],
          [0, y + 0.055, -0.27 + k * 0.083],
          "accent",
          "deck_spine_grid",
        );
        for (const s of [-1, 1])
          b.box(
            [0.008, 0.014, 0.028],
            [s * 0.3, y, -0.27 + k * 0.083],
            "glow",
            "habitat_light_band",
          );
      }
    }
    for (const s of [-1, 1])
      for (const z of [-0.23, 0.26])
        b.box(
          [0.075, 0.37, 0.09],
          [s * 0.22, 0, z],
          "metal",
          "vertical_load_tower",
        );
  }
}
function cryogenic(b, role) {
  const i = [
    "warp_scout",
    "science_vessel",
    "patrol_corvette",
    "bulk_freighter",
    "resource_outpost_ship",
    "colony_ship",
  ].indexOf(role);
  b.cyl(
    0.054,
    0.054,
    0.83,
    [0, 0, -0.02],
    "metal",
    "thermal_isolation_spine",
    [Math.PI / 2, 0, 0],
    6,
  );
  b.cyl(
    0.15,
    0.105,
    0.28,
    [0, 0, -0.28],
    "hull",
    "cold_habitat_lantern",
    [Math.PI / 2, 0, 0],
    6,
  );
  b.cyl(
    0.1,
    0.145,
    0.12,
    [0, 0, -0.47],
    "hull",
    "forward_ice_cap",
    [Math.PI / 2, 0, 0],
    6,
  );
  b.cyl(
    0.115,
    0.125,
    0.025,
    [0, 0, -0.395],
    "glass",
    "radial_observation_band",
    [Math.PI / 2, 0, 0],
    6,
  );
  for (let j = 0; j < 6; j++) {
    const a = (j * Math.PI) / 3,
      x = Math.sin(a),
      y = Math.cos(a);
    b.beam(
      [x * 0.145, y * 0.145, -0.36],
      [x * 0.19, y * 0.19, -0.08],
      0.008,
      "accent",
      "cold_shell_standoff",
    );
    b.beam(
      [x * 0.085, y * 0.085, -0.09],
      [x * 0.23, y * 0.23, 0.31],
      0.009,
      "metal",
      "thermal_drive_boom",
    );
    b.cyl(
      0.028,
      0.038,
      0.12,
      [x * 0.23, y * 0.23, 0.32],
      "dark",
      "isolated_hot_drive",
      [Math.PI / 2, 0, 0],
      6,
    );
    b.cyl(
      0.019,
      0.019,
      0.004,
      [x * 0.23, y * 0.23, 0.382],
      "glow",
      "drive_glow",
      [Math.PI / 2, 0, 0],
      6,
    );
    const p = b.box(
      [0.095, 0.007, 0.25],
      [x * 0.19, y * 0.19, 0.09],
      "radiator",
      "heat_rejection_fin",
      [0, 0, -a],
    );
    for (let k = 0; k < (b.seg === 8 ? 2 : 5); k++)
      b.box(
        [0.09, 0.009, 0.003],
        [x * 0.19, y * 0.19, 0.0 + k * 0.04],
        "metal",
        "radiator_rib",
        [0, 0, -a],
      );
    b.ell(
      [0.009, 0.009, 0.028],
      [x * 0.139, y * 0.139, -0.27],
      "glow",
      "cold_navigation",
    );
  }
  for (const s of [-1, 1]) docking(b, s * 0.152, 0, -0.24);
  if (i === 1) {
    b.tor(0.285, 0.014, [0, 0, -0.18], "metal", "sixfold_science_frame");
    for (let j = 0; j < 6; j++) {
      const a = (j * Math.PI) / 3,
        x = Math.sin(a) * 0.275,
        y = Math.cos(a) * 0.275;
      b.cyl(
        0.061,
        0.05,
        0.15,
        [x, y, -0.19],
        "hull",
        "cold_laboratory_lantern",
        [Math.PI / 2, 0, 0],
        6,
      );
      b.beam([0, 0, -0.18], [x, y, -0.18], 0.008);
      b.cyl(
        0.04,
        0.04,
        0.008,
        [x, y, -0.275],
        "glass",
        "sensor_window",
        [Math.PI / 2, 0, 0],
        6,
      );
    }
  }
  if (i === 2) {
    for (const s of [-1, 1]) {
      b.loft(
        [
          [-0.49, 0.008, 0.014],
          [-0.31, 0.035, 0.025],
          [0.22, 0.044, 0.025],
          [0.33, 0.022, 0.018],
        ],
        "hull",
        "split_guard_lance",
      ).position.x = s * 0.18;
      b.beam([0, 0, -0.27], [s * 0.18, 0, -0.32], 0.012, "accent");
    }
    b.cyl(
      0.08,
      0.11,
      0.16,
      [0, 0, 0.23],
      "hull",
      "protected_drive_root",
      [Math.PI / 2, 0, 0],
      6,
    );
  }
  if (i >= 3) {
    const layers = i === 3 ? 2 : i === 4 ? 3 : 4;
    for (let k = 0; k < layers; k++)
      for (let j = 0; j < 6; j++) {
        const a = (j * Math.PI) / 3,
          x = Math.sin(a) * 0.265,
          y = Math.cos(a) * 0.265,
          z = -0.3 + k * 0.17;
        b.cyl(
          0.075,
          0.067,
          0.13,
          [x, y, z],
          "hull",
          i === 3 ? "cryofreight_lantern" : "insulated_habitat_lantern",
          [Math.PI / 2, 0, 0],
          6,
        );
        b.cyl(
          0.077,
          0.077,
          0.009,
          [x, y, z - 0.037],
          "accent",
          "thermal_partition",
          [Math.PI / 2, 0, 0],
          6,
        );
        b.beam([0, 0, z], [x, y, z], 0.01);
      }
  }
  if (i === 4)
    b.cyl(
      0.16,
      0.18,
      0.11,
      [0, 0, -0.46],
      "dark",
      "outpost_seed_capsule",
      [Math.PI / 2, 0, 0],
      6,
    );
  if (i === 5) {
    for (const z of [-0.32, 0, 0.27]) {
      b.tor(0.38, 0.015, [0, 0, z], "metal", "ark_hexaframe");
      for (let j = 0; j < 6; j++) {
        const a = (j * Math.PI) / 3;
        b.cyl(
          0.05,
          0.05,
          0.075,
          [Math.sin(a) * 0.38, Math.cos(a) * 0.38, z],
          "hull",
          "sixfold_access_hub",
          [Math.PI / 2, 0, 0],
          6,
        );
      }
    }
    for (let j = 0; j < 6; j++) {
      const a = (j * Math.PI) / 3;
      b.beam(
        [Math.sin(a) * 0.38, Math.cos(a) * 0.38, -0.32],
        [Math.sin(a) * 0.38, Math.cos(a) * 0.38, 0.27],
        0.009,
        "accent",
        "ark_tension_member",
      );
    }
  }
}
export function createHull(ship, { lod = 0 } = {}) {
  const race = getRace(ship.raceId),
    role = getRole(ship.roleId),
    root = new THREE.Group();
  root.name = ship.id;
  root.userData = {
    assetId: ship.id,
    speciesId: ship.raceId,
    designId: ship.roleId,
    units: "metres",
    forward: "-Z",
    up: "+Y",
    optionalEquipment: "separate meshes at HP_* nodes",
    stage: ship.stage,
  };
  const physical = new THREE.Group();
  physical.name = "HULL";
  root.add(physical);
  const p = materials(race),
    b = builder(physical, p, lod);
  ({
    pelagic_high_pressure: pelagic,
    compact_high_gravity: compact,
    cryogenic_hydrocarbon: cryogenic,
  })[race.id](b, role.id);
  physical.updateMatrixWorld(true);
  let box = new THREE.Box3().setFromObject(physical);
  const c = box.getCenter(V()),
    sz = box.getSize(V());
  physical.position.copy(c).negate();
  const wrapper = new THREE.Group();
  root.remove(physical);
  wrapper.add(physical);
  root.add(wrapper);
  wrapper.name = "HULL_METRE_SCALE";
  wrapper.scale.set(
    ship.dimensions.width / sz.x,
    ship.dimensions.height / sz.y,
    ship.dimensions.length / sz.z,
  );
  root.updateMatrixWorld(true);
  const canonical = lod ? createHull(ship, { lod: 0 }) : null;
  const sockets = [],
    counts = {
      weapon: role.weapon,
      utility: role.utility,
      defense: role.defense,
      cargo: role.cargo,
      thermal: role.thermal,
    };
  const prefixes = {
    weapon: "W",
    utility: "U",
    defense: "D",
    cargo: "C",
    thermal: "T",
  };
  const ray = new THREE.Raycaster(),
    L = ship.dimensions.length,
    W = ship.dimensions.width,
    H = ship.dimensions.height;
  for (const [kind, count] of Object.entries(counts))
    for (let n = 0; n < count; n++) {
      const side = n % 2 === 0 ? -1 : 1,
        pair = Math.floor(n / 2),
        pairs = Math.ceil(count / 2);
      let x, z, normal;
      if (kind === "weapon") {
        x = side * W * 0.17;
        z = L * (-0.28 + (pairs === 1 ? 0 : (pair * 0.46) / (pairs - 1)));
        normal = V(0, 1, 0);
      }
      if (kind === "utility") {
        x = side * W * 0.075;
        z = L * (-0.17 + pair * 0.2);
        normal = V(0, 1, 0);
      }
      if (kind === "defense") {
        x = side * W * 0.23;
        z = L * (-0.06 + pair * 0.23);
        normal = V(0, -1, 0);
      }
      if (kind === "cargo") {
        x = side * W * 0.32;
        z = L * (-0.25 + pair * 0.25);
        normal = V(0, 1, 0);
      }
      if (kind === "thermal") {
        x = side * W * 0.15;
        z = L * 0.29;
        normal = V(0, 1, 0);
      }
      // Intersect the actual authored hull so the attachment sits on a surface.
      let origin = V(x, normal.y * H * 2, z);
      ray.set(origin, normal.clone().negate());
      let hit = ray.intersectObject(wrapper, true)[0];
      if (!hit) {
        for (const factor of [0.8, 0.6, 0.4, 0.2, 0.08, 0]) {
          origin.x = x * factor;
          ray.set(origin, normal.clone().negate());
          hit = ray.intersectObject(wrapper, true)[0];
          if (hit) break;
        }
      }
      if (!hit) throw new Error(`Cannot place ${kind} ${n} on ${ship.id}`);
      const maxSize =
        kind === "weapon"
          ? role.id === "warp_scout"
            ? "M"
            : "L"
          : kind === "utility"
            ? "M"
            : kind === "thermal"
              ? "M"
              : "L";
      const footprint = sizeMeters[maxSize];
      const node = new THREE.Object3D();
      node.name = `HP_${prefixes[kind]}${String(n + 1).padStart(2, "0")}`;
      node.position.copy(hit.point).addScaledVector(normal, 0.08);
      if (canonical)
        node.position.fromArray(canonical.sockets[sockets.length].position);
      node.quaternion.setFromUnitVectors(V(0, 1, 0), normal);
      node.userData = {
        kind,
        size: maxSize,
        id: node.name,
        points: ship.points,
        physicalMountWidthMeters: footprint,
        forward: "-Z",
        outward: "+Y",
        researchBinding: "pending",
      };
      root.add(node);
      const mount = new THREE.Group();
      mount.name = `MOUNT_${node.name}`;
      mount.position.copy(node.position);
      mount.quaternion.copy(node.quaternion);
      const mb = builder(mount, p, lod);
      mb.cyl(
        footprint * 0.48,
        footprint * 0.52,
        0.18,
        [0, 0.0, 0],
        "dark",
        "integral_mount_plate",
        [0, 0, 0],
        race.id === "cryogenic_hydrocarbon"
          ? 6
          : race.id === "compact_high_gravity"
            ? 4
            : 12,
      );
      mb.cyl(
        footprint * 0.3,
        footprint * 0.3,
        0.2,
        [0, 0.03, 0],
        "metal",
        "socket_interface",
        [0, 0, 0],
        8,
      );
      root.add(mount);
      sockets.push({
        id: node.name,
        kind,
        size: maxSize,
        position: node.position.toArray(),
        rotationQuaternion: node.quaternion.toArray(),
        moduleForward: "-Z",
        moduleOutward: "+Y",
        footprintMeters: footprint,
      });
    }
  if (canonical) disposeTree(canonical.root);
  root.userData.socketCount = sockets.length;
  root.updateMatrixWorld(true);
  return { root, sockets, ship, race };
}
export function createModule(raceId, moduleId, { lod = 0 } = {}) {
  const race = getRace(raceId),
    def = getModule(moduleId);
  if (!race || !def) throw new Error("Unknown module/race");
  const root = new THREE.Group();
  root.name = `MOD_${raceId}_${moduleId}`;
  root.userData = {
    speciesId: raceId,
    moduleId,
    kind: def.kind,
    size: def.size,
    units: "metres",
    outward: "+Y",
    forward: "-Z",
    ...def,
  };
  const p = materials(race),
    b = builder(root, p, lod);
  const compactRace = raceId === "compact_high_gravity",
    cold = raceId === "cryogenic_hydrocarbon";
  b.cyl(
    0.34,
    0.42,
    0.1,
    [0, 0.05, 0],
    "dark",
    "mount_adapter",
    [0, 0, 0],
    cold ? 6 : compactRace ? 4 : 16,
  );
  b.cyl(
    0.27,
    0.31,
    0.09,
    [0, 0.145, 0],
    "metal",
    "rotation_bearing",
    [0, 0, 0],
    12,
  );
  const housing = (s, pos) =>
    compactRace
      ? b.box(s, pos, "hull", "armored_module_housing")
      : cold
        ? b.cyl(
            s[0] * 0.5,
            s[0] * 0.5,
            s[1],
            pos,
            "hull",
            "faceted_module_housing",
            [0, 0, 0],
            6,
          )
        : b.ell(
            s.map((v) => v * 0.5),
            pos,
            "hull",
            "pressure_module_housing",
          );
  if (["beam_turret", "kinetic_turret", "point_defense"].includes(moduleId)) {
    housing([0.8, 0.42, 0.68], [0, 0.34, 0]);
    const num = moduleId === "point_defense" ? 4 : 2;
    for (let j = 0; j < num; j++) {
      const x = (j - (num - 1) / 2) * 0.17,
        z = moduleId === "kinetic_turret" ? -0.8 : -0.57,
        len = moduleId === "kinetic_turret" ? 1.15 : 0.75;
      b.box([0.085, 0.09, len], [x, 0.42, z], "metal", "weapon_barrel");
      b.box([0.11, 0.12, 0.12], [x, 0.42, z - len * 0.5], "dark", "muzzle");
      if (moduleId === "beam_turret")
        b.box(
          [0.035, 0.03, 0.018],
          [x, 0.42, z - len * 0.5 - 0.061],
          "glow",
          "beam_aperture",
        );
      if (moduleId === "kinetic_turret")
        for (const side of [-1, 1])
          b.box(
            [0.018, 0.13, 0.67],
            [x + side * 0.065, 0.42, -0.82],
            "accent",
            "rail_accelerator",
          );
    }
    b.box([0.16, 0.09, 0.2], [0, 0.61, 0.06], "glass", "targeting_unit");
  }
  if (moduleId === "missile_pod") {
    housing([0.95, 0.5, 0.9], [0, 0.4, 0.02]);
    for (const x of [-0.27, 0, 0.27])
      for (const y of [0.31, 0.57]) {
        b.cyl(
          0.091,
          0.091,
          0.35,
          [x, y, -0.43],
          "dark",
          "launch_tube",
          [Math.PI / 2, 0, 0],
          8,
        );
        b.cyl(
          0.063,
          0.063,
          0.013,
          [x, y, -0.61],
          "accent",
          "launcher_cap",
          [Math.PI / 2, 0, 0],
          8,
        );
      }
  }
  if (moduleId === "sensor_array") {
    b.cyl(0.065, 0.12, 0.6, [0, 0.45, 0], "metal", "sensor_mast", [0, 0, 0]);
    b.cyl(
      0.47,
      0.16,
      0.13,
      [0, 0.83, 0],
      "hull",
      "sensor_dish",
      [Math.PI / 2 + 0.5, 0, 0],
      cold ? 6 : 20,
    );
    b.cyl(
      0.4,
      0.14,
      0.015,
      [0, 0.88, -0.057],
      "glass",
      "array_face",
      [Math.PI / 2 + 0.5, 0, 0],
      cold ? 6 : 20,
    );
    b.beam([0, 0.83, 0], [0, 1.03, -0.42], 0.018, "accent", "receiver");
  }
  if (moduleId === "command_relay") {
    b.cyl(0.025, 0.07, 1.2, [0, 0.78, 0], "metal", "relay_mast", [0, 0, 0]);
    for (let j = 0; j < (cold ? 6 : 4); j++) {
      const a = (j * Math.PI) / (cold ? 3 : 2);
      b.box(
        [0.1, 0.48, 0.028],
        [Math.cos(a) * 0.19, 0.96, Math.sin(a) * 0.19],
        "accent",
        "relay_vane",
        [0, -a, 0],
      );
    }
    b.ell([0.065, 0.065, 0.065], [0, 1.4, 0], "glow", "signal_tip");
  }
  if (moduleId === "shield_emitter") {
    b.ell([0.26, 0.32, 0.26], [0, 0.52, 0], "glow", "shield_emitter_core");
    const n = cold ? 6 : 4;
    for (let j = 0; j < n; j++) {
      const a = (j * Math.PI * 2) / n;
      b.beam(
        [Math.cos(a) * 0.37, 0.2, Math.sin(a) * 0.37],
        [Math.cos(a) * 0.28, 0.83, Math.sin(a) * 0.28],
        0.055,
        "hull",
        "emitter_cage",
      );
    }
    b.tor(0.28, 0.038, [0, 0.83, 0], "metal", "emitter_ring", [
      Math.PI / 2,
      0,
      0,
    ]);
  }
  if (moduleId === "armor_plate") {
    for (let j = 0; j < 3; j++)
      b.box(
        [1.04 - j * 0.1, 0.12, 1.2 - j * 0.12],
        [0, 0.2 + j * 0.1, 0],
        j === 1 ? "accent" : "hull",
        "layered_armor",
      );
    for (const s of [-1, 1])
      b.box([0.035, 0.02, 0.8], [s * 0.3, 0.46, 0], "metal", "armor_brace");
  }
  if (moduleId === "cargo_pod" || moduleId === "habitat_pod") {
    if (compactRace) {
      b.box([0.88, 0.8, 1.65], [0, 0.59, 0], "hull", "freight_box");
      for (const z of [-0.61, 0, 0.61])
        b.box([0.94, 0.83, 0.07], [0, 0.59, z], "accent", "load_band");
    } else {
      b.cyl(
        0.45,
        0.45,
        1.45,
        [0, 0.65, 0],
        "hull",
        "sealed_pod",
        [Math.PI / 2, 0, 0],
        cold ? 6 : 20,
      );
      for (const z of [-0.61, 0, 0.61])
        b.cyl(
          0.465,
          0.465,
          0.045,
          [0, 0.65, z],
          "accent",
          "containment_band",
          [Math.PI / 2, 0, 0],
          cold ? 6 : 20,
        );
      b.cyl(
        0.4,
        0.31,
        0.13,
        [0, 0.65, -0.79],
        "dark",
        "pod_end",
        [Math.PI / 2, 0, 0],
        cold ? 6 : 20,
      );
    }
    if (moduleId === "habitat_pod")
      for (const s of [-1, 1])
        for (let k = 0; k < 5; k++)
          b.box(
            [0.02, 0.085, 0.12],
            [s * 0.45, 0.71, -0.5 + k * 0.25],
            "glass",
            "habitat_view_port",
          );
  }
  if (moduleId === "radiator") {
    b.box([0.11, 0.45, 0.1], [0, 0.38, 0], "metal", "thermal_riser");
    for (const s of [-1, 1]) {
      b.box(
        [0.75, 0.035, 1.1],
        [s * 0.42, 0.61, 0],
        "radiator",
        "radiator_panel",
      );
      for (let k = 0; k < 9; k++)
        b.box(
          [0.74, 0.04, 0.015],
          [s * 0.42, 0.61, -0.5 + k * 0.125],
          "metal",
          "thermal_channel",
        );
      b.box([0.035, 0.055, 1.15], [s * 0.8, 0.61, 0], "accent", "panel_edge");
    }
  }
  if (moduleId === "power_unit") {
    housing([0.7, 0.62, 1.1], [0, 0.48, 0]);
    for (const x of [-0.37, 0.37])
      for (let k = 0; k < 5; k++)
        b.box(
          [0.08, 0.43, 0.065],
          [x, 0.48, -0.4 + k * 0.2],
          "metal",
          "cooling_fin",
        );
    b.ell([0.15, 0.06, 0.22], [0, 0.81, 0], "glow", "power_status");
  }
  // Modules are built in nominal-size units, not scaled with the host ship.
  root.scale.setScalar(sizeMeters[def.size]);
  root.updateMatrixWorld(true);
  return root;
}
export function attachModule(hull, socketId, moduleId) {
  const socket = hull.sockets.find((x) => x.id === socketId);
  if (!socket || !compatible(socket, getModule(moduleId)))
    throw new Error(`Incompatible attachment ${socketId}/${moduleId}`);
  const node = hull.root.getObjectByName(socketId);
  while (node.children.length) {
    disposeTree(node.children[0]);
    node.remove(node.children[0]);
  }
  const model = createModule(hull.ship.raceId, moduleId);
  node.add(model);
  return model;
}
export function clearFittings(hull) {
  for (const s of hull.sockets) {
    const n = hull.root.getObjectByName(s.id);
    while (n.children.length) {
      disposeTree(n.children[0]);
      n.remove(n.children[0]);
    }
  }
}
export function presetFittings(hull) {
  const fitting = {};
  for (const s of hull.sockets) {
    let id = {
      weapon:
        hull.ship.roleId === "patrol_corvette"
          ? "kinetic_turret"
          : "point_defense",
      utility: "sensor_array",
      defense: "shield_emitter",
      cargo: "cargo_pod",
      thermal: "radiator",
    }[s.kind];
    if (
      s.kind === "weapon" &&
      s.id === "HP_W02" &&
      hull.ship.roleId === "patrol_corvette"
    )
      id = "beam_turret";
    const candidate = { ...fitting, [s.id]: id };
    if (checkFitting(hull.ship, hull.sockets, candidate).valid)
      fitting[s.id] = id;
  }
  return fitting;
}
export function applyFittings(hull, fittings) {
  const result = checkFitting(hull.ship, hull.sockets, fittings);
  if (!result.valid) throw new Error(result.errors.join("; "));
  clearFittings(hull);
  for (const [s, m] of Object.entries(fittings)) attachModule(hull, s, m);
  return result;
}
export function disposeTree(root) {
  const geometries = new Set(),
    materials = new Set();
  root.traverse((o) => {
    if (o.geometry) geometries.add(o.geometry);
    if (o.material)
      (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) =>
        materials.add(m),
      );
  });
  geometries.forEach((g) => g.dispose());
  materials.forEach((m) => m.dispose());
}
