import * as THREE from "three";
import { getMetalTextures } from "./metal-textures.mjs";
import {
  mergeGeometries,
  mergeVertices,
} from "three/addons/utils/BufferGeometryUtils.js";
import { RoundedBoxGeometry } from "three/addons/geometries/RoundedBoxGeometry.js";

const vector = (x = 0, y = 0, z = 0) => new THREE.Vector3(x, y, z);
const noise = (n) => {
  const x = Math.sin(n * 127.1 + 311.7) * 43758.5453123;
  return x - Math.floor(x);
};

// Real geometry and standard PBR materials: construction details survive GLB export.
// Each material's small parts are merged to avoid one draw call per plate/fastener.
export function metalMaterials(race) {
  const cold = race.id === "cryogenic_hydrocarbon",
    heavy = race.id === "compact_high_gravity";
  const base = new THREE.Color(
    cold ? "#d6d9dc" : heavy ? "#a4adb7" : "#bbc4c9",
  );
  const mats = Array.from(
    { length: 5 },
    (_, i) =>
      new THREE.MeshStandardMaterial({
        name: `METAL_PLATE_${i}`,
        color: base.clone().multiplyScalar(0.82 + i * 0.075),
        metalness: 0.86,
        roughness: 0.39 + i * 0.035,
      }),
  );
  mats.push(
    new THREE.MeshStandardMaterial({
      name: "RECESSED_MACHINERY",
      color: "#171d24",
      metalness: 0.72,
      roughness: 0.65,
    }),
  );
  mats.push(
    new THREE.MeshStandardMaterial({
      name: "BRUSHED_EDGE",
      color: "#959b9e",
      metalness: 0.94,
      roughness: 0.31,
    }),
  );
  mats.push(
    new THREE.MeshStandardMaterial({
      name: "THERMAL_ALLOY",
      color: cold ? "#8b887c" : heavy ? "#82705c" : "#5c6e75",
      metalness: 0.88,
      roughness: 0.47,
    }),
  );
  const maps = getMetalTextures();
  for (const m of mats) {
    m.map = maps.albedo;
    m.roughnessMap = maps.orm;
    m.metalnessMap = maps.orm;
    m.normalMap = maps.normal;
    m.normalScale = new THREE.Vector2(0.35, 0.35);
    m.roughness = 1;
  }
  return mats;
}

function collector(parent, mats) {
  const buckets = mats.map(() => []);
  return {
    add(geometry, material = 0) {
      buckets[material].push(
        geometry.index ? geometry.toNonIndexed() : geometry,
      );
      if (geometry.index) geometry.dispose();
    },
    finish() {
      for (let i = 0; i < buckets.length; i++) {
        if (!buckets[i].length) continue;
        const merged = mergeGeometries(buckets[i], false);
        buckets[i].forEach((g) => g.dispose());
        const welded = mergeVertices(merged, 1e-7);
        merged.dispose();
        const mesh = new THREE.Mesh(welded, mats[i]);
        mesh.name = `ASSEMBLED_${mats[i].name}`;
        mesh.castShadow = true;
        mesh.receiveShadow = true;
        parent.add(mesh);
      }
    },
  };
}
function tileQuad(a, b, c, d, offset = 0) {
  const normal = d.clone().sub(a).cross(b.clone().sub(a)).normalize();
  const verts = [a, b, c, d].map((p) =>
    p.clone().addScaledVector(normal, offset),
  );
  const g = new THREE.BufferGeometry();
  g.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      verts.flatMap((p) => p.toArray()),
      3,
    ),
  );
  g.setAttribute(
    "uv",
    new THREE.Float32BufferAttribute([0, 0, 1, 0, 1, 1, 0, 1], 2),
  );
  g.setIndex([0, 3, 1, 1, 3, 2]);
  g.computeVertexNormals();
  return {
    g,
    normal,
    center: a.clone().add(b).add(c).add(d).multiplyScalar(0.25),
  };
}
function gadget(parts, center, normal, width, seed, lod) {
  const q = new THREE.Quaternion().setFromUnitVectors(vector(0, 1, 0), normal);
  const put = (geometry, x, y, z, mat) => {
    geometry.applyMatrix4(
      new THREE.Matrix4().compose(
        center.clone().add(vector(x, y, z).applyQuaternion(q)),
        q,
        vector(1, 1, 1),
      ),
    );
    parts.add(geometry, mat);
  };
  const n = noise(seed),
    w = width;
  if (n < 0.38) {
    put(new THREE.BoxGeometry(w * 0.72, w * 0.02, w * 0.82), 0, w * 0.01, 0, 5);
    for (let k = 0; k < (lod ? 3 : 7); k++)
      put(
        new THREE.BoxGeometry(w * 0.61, w * 0.034, w * 0.038),
        0,
        w * 0.035,
        (k - (lod ? 1 : 3)) * w * (lod ? 0.22 : 0.105),
        6,
      );
  } else if (n < 0.68) {
    put(
      new RoundedBoxGeometry(w * 0.64, w * 0.06, w * 0.8, 1, w * 0.014),
      0,
      w * 0.045,
      0,
      noise(seed + 2) > 0.5 ? 1 : 3,
    );
    put(
      new THREE.BoxGeometry(w * 0.035, w * 0.075, w * 0.5),
      w * 0.21,
      w * 0.072,
      0,
      6,
    );
    if (!lod)
      for (const x of [-0.24, 0.24])
        for (const z of [-0.3, 0.3])
          put(
            new THREE.CylinderGeometry(w * 0.025, w * 0.025, w * 0.016, 6),
            w * x,
            w * 0.085,
            w * z,
            6,
          );
  } else {
    put(
      new THREE.BoxGeometry(w * 0.76, w * 0.027, w * 0.6),
      0,
      w * 0.012,
      0,
      5,
    );
    for (const x of [-0.24, 0, 0.24])
      put(
        new THREE.CylinderGeometry(w * 0.033, w * 0.033, w * 0.61, 6).rotateX(
          Math.PI / 2,
        ),
        x * w,
        w * 0.09,
        0,
        7,
      );
    for (const z of [-0.22, 0.22])
      put(
        new THREE.BoxGeometry(w * 0.83, w * 0.038, w * 0.04),
        0,
        w * 0.065,
        z * w,
        6,
      );
  }
}

function plateSphere(mesh, mats, lod) {
  const parts = collector(mesh, mats),
    columns = lod ? 14 : 26,
    rows = lod ? 8 : 14;
  mesh.material = mats[5];
  mesh.geometry.scale(0.98, 0.98, 0.98);
  for (let row = 0; row < rows; row++)
    for (let col = 0; col < columns; col++) {
      const p0 = (col * Math.PI * 2) / columns + 0.0015,
        p1 = (Math.PI * 2) / columns - 0.003,
        t0 = (row * Math.PI) / rows + 0.0008,
        t1 = Math.PI / rows - 0.0016;
      const geometry = new THREE.SphereGeometry(
        1.007,
        lod ? 2 : 3,
        2,
        p0,
        p1,
        t0,
        t1,
      );
      parts.add(geometry, Math.floor(noise(row * 53 + col * 17) * 5));
      // Fasteners and small service plates sit on the pressure shell, not above it.
      if (!lod && row > 1 && row < rows - 2 && (row + col) % 4 === 0) {
        const phi = p0 + p1 * 0.5,
          theta = t0 + t1 * 0.5;
        const normal = vector(
          -Math.cos(phi) * Math.sin(theta),
          Math.cos(theta),
          Math.sin(phi) * Math.sin(theta),
        );
        const bolt = new THREE.CylinderGeometry(0.008, 0.008, 0.005, 6);
        bolt.applyQuaternion(
          new THREE.Quaternion().setFromUnitVectors(vector(0, 1, 0), normal),
        );
        bolt.translate(...normal.clone().multiplyScalar(1.012).toArray());
        parts.add(bolt, 6);
      }
    }
  parts.finish();
}

function plateCylinder(mesh, mats, lod) {
  const {
    radiusTop: rt,
    radiusBottom: rb,
    height: h,
    radialSegments: n,
  } = mesh.geometry.parameters;
  if (h < Math.max(rt, rb) * 0.35) return;
  const parts = collector(mesh, mats),
    sides = n <= 8 ? n : lod ? 12 : 24,
    rows = lod ? 3 : 7;
  mesh.material = mats[5];
  mesh.geometry.scale(0.985, 1, 0.985);
  for (let row = 0; row < rows; row++)
    for (let side = 0; side < sides; side++) {
      const v0 = row / rows + 0.008,
        v1 = (row + 1) / rows - 0.008,
        r0 = rb + (rt - rb) * v0,
        r1 = rb + (rt - rb) * v1;
      let g;
      if (n <= 8) {
        const a = (side * Math.PI * 2) / n,
          b = ((side + 1) * Math.PI * 2) / n;
        const point = (angle, r, y) =>
          vector(Math.sin(angle) * r, y, Math.cos(angle) * r);
        const aa = point(a, r0, -h * 0.5 + h * v0),
          bb = point(b, r0, -h * 0.5 + h * v0),
          cc = point(b, r1, -h * 0.5 + h * v1),
          dd = point(a, r1, -h * 0.5 + h * v1);
        const left = aa.clone().lerp(bb, 0.018),
          right = bb.clone().lerp(aa, 0.018),
          topRight = cc.clone().lerp(dd, 0.018),
          topLeft = dd.clone().lerp(cc, 0.018);
        // Cylinder side winding is the reverse of the longitudinal hull loft.
        const tile = tileQuad(
          left,
          topLeft,
          topRight,
          right,
          Math.max(rt, rb) * 0.007,
        );
        g = tile.g;
        if (row % 2 === 1 && !lod && side % 2 === 0)
          gadget(
            parts,
            tile.center.clone().addScaledVector(tile.normal, rt * 0.012),
            tile.normal,
            Math.min(rt * 0.24, h * 0.09),
            row * 31 + side,
            0,
          );
      } else
        g = new THREE.CylinderGeometry(
          r1 * 1.007,
          r0 * 1.007,
          (v1 - v0) * h,
          3,
          1,
          true,
          (side * Math.PI * 2) / sides + 0.007,
          (Math.PI * 2) / sides - 0.014,
        ).translate(0, -h * 0.5 + (v0 + v1) * h * 0.5, 0);
      parts.add(g, Math.floor(noise(row * 41 + side * 7) * 5));
    }
  for (const sign of [-1, 1]) {
    const r = sign > 0 ? rt : rb;
    for (let side = 0; side < (n <= 8 ? n : 12); side++) {
      const count = n <= 8 ? n : 12;
      const g = new THREE.RingGeometry(
        r * 0.17,
        r * 0.985,
        n <= 8 ? 1 : 3,
        1,
        (side * Math.PI * 2) / count + 0.009,
        (Math.PI * 2) / count - 0.018,
      );
      g.rotateX(sign > 0 ? -Math.PI / 2 : Math.PI / 2).translate(
        0,
        sign * (h * 0.5 + r * 0.007),
        0,
      );
      parts.add(g, Math.floor(noise(side + 71) * 5));
    }
  }
  // Fine longitudinal rails make the segmented casings read as assembled hardware.
  for (let k = 0; k < (n <= 8 ? n : 8); k++) {
    const a = (k * Math.PI * 2) / (n <= 8 ? n : 8),
      r = (rt + rb) * 0.5;
    const g = new THREE.BoxGeometry(r * 0.023, h * 0.88, r * 0.026).translate(
      Math.sin(a) * r * 1.012,
      0,
      Math.cos(a) * r * 1.012,
    );
    parts.add(g, 7);
  }
  parts.finish();
}

function plateBox(mesh, mats, lod) {
  const { width: w, height: h, depth: d } = mesh.geometry.parameters;
  if (!w || Math.min(w, d) < 0.05) return;
  const parts = collector(mesh, mats);
  mesh.material = mats[5];
  mesh.geometry.scale(0.998, 0.998, 0.998);
  const faces = [
    {
      origin: vector(-w / 2, h / 2, d / 2),
      u: vector(w, 0, 0),
      v: vector(0, 0, -d),
    },
    {
      origin: vector(-w / 2, -h / 2, -d / 2),
      u: vector(w, 0, 0),
      v: vector(0, 0, d),
    },
    {
      origin: vector(w / 2, -h / 2, -d / 2),
      u: vector(0, h, 0),
      v: vector(0, 0, d),
    },
    {
      origin: vector(-w / 2, -h / 2, d / 2),
      u: vector(0, h, 0),
      v: vector(0, 0, -d),
    },
    {
      origin: vector(-w / 2, -h / 2, -d / 2),
      u: vector(w, 0, 0),
      v: vector(0, h, 0),
    },
    {
      origin: vector(w / 2, -h / 2, d / 2),
      u: vector(-w, 0, 0),
      v: vector(0, h, 0),
    },
  ];
  for (let f = 0; f < faces.length; f++) {
    const { origin, u, v } = faces[f],
      nu = Math.max(
        2,
        Math.min(lod ? 5 : 12, Math.ceil(u.length() / (lod ? 0.09 : 0.045))),
      ),
      nv = Math.max(
        2,
        Math.min(lod ? 5 : 12, Math.ceil(v.length() / (lod ? 0.1 : 0.06))),
      );
    for (let x = 0; x < nu; x++)
      for (let y = 0; y < nv; y++) {
        const gap = 0.004,
          point = (a, b) =>
            origin.clone().addScaledVector(u, a).addScaledVector(v, b),
          a = point((x + gap) / nu, (y + gap) / nv),
          b = point((x + 1 - gap) / nu, (y + gap) / nv),
          c = point((x + 1 - gap) / nu, (y + 1 - gap) / nv),
          dd = point((x + gap) / nu, (y + 1 - gap) / nv);
        const tile = tileQuad(a, dd, c, b, 0.0009);
        parts.add(tile.g, Math.floor(noise(x * 37 + y * 91 + f * 133) * 5));
        if (!lod && f === 0 && (x + y) % 3 === 0)
          gadget(
            parts,
            tile.center.clone().addScaledVector(tile.normal, 0.001),
            tile.normal,
            Math.min(u.length() / nu, v.length() / nv) * 0.65,
            x * 37 + y * 91,
            0,
          );
      }
  }
  parts.finish();
}

function plateLoft(mesh, mats, lod) {
  const sections = mesh.userData.loftSections;
  if (!sections) return;
  const p = mesh.geometry.attributes.position,
    parts = collector(mesh, mats);
  mesh.material = mats[5];
  for (let k = 0; k < sections.length - 1; k++)
    for (let side = 0; side < 6; side++) {
      const a = vector().fromBufferAttribute(p, k * 6 + side),
        b = vector().fromBufferAttribute(p, k * 6 + ((side + 1) % 6)),
        c = vector().fromBufferAttribute(p, (k + 1) * 6 + ((side + 1) % 6)),
        d = vector().fromBufferAttribute(p, (k + 1) * 6 + side);
      const nu = Math.max(
          2,
          Math.min(lod ? 4 : 10, Math.ceil(a.distanceTo(b) / 0.045)),
        ),
        nv = Math.max(
          2,
          Math.min(lod ? 5 : 12, Math.ceil(a.distanceTo(d) / 0.05)),
        );
      const point = (u, v) =>
        a.clone().lerp(b, u).lerp(d.clone().lerp(c, u), v);
      for (let x = 0; x < nu; x++)
        for (let y = 0; y < nv; y++) {
          const margin = 0.004,
            tile = tileQuad(
              point((x + margin) / nu, (y + margin) / nv),
              point((x + 1 - margin) / nu, (y + margin) / nv),
              point((x + 1 - margin) / nu, (y + 1 - margin) / nv),
              point((x + margin) / nu, (y + 1 - margin) / nv),
              0.001,
            );
          parts.add(
            tile.g,
            Math.floor(noise(k * 41 + side * 13 + x * 7 + y * 67) * 5),
          );
          if (!lod && tile.normal.y > 0.3 && (x + y) % 3 === 0)
            gadget(
              parts,
              tile.center.clone().addScaledVector(tile.normal, 0.0014),
              tile.normal,
              Math.min(a.distanceTo(b) / nu, a.distanceTo(d) / nv) * 0.7,
              x * 7 + y * 67 + k * 13,
              0,
            );
        }
    }
  parts.finish();
}

export function addMetalConstruction(
  root,
  race,
  { lod = 0, module = false } = {},
) {
  const mats = metalMaterials(race),
    original = [];
  root.traverse((o) => {
    if (o.isMesh) original.push(o);
  });
  for (const mesh of original) {
    const name = mesh.name;
    if (
      /bioluminescent_navigation|cold_navigation|navigation_inlay|pressure_bulkhead|cell_pressure_band/.test(
        name,
      )
    ) {
      mesh.removeFromParent();
      mesh.geometry.dispose();
      continue;
    }
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    const type = mesh.geometry.type,
      eligible =
        mesh.material.name === "hull" ||
        !!mesh.userData.loftSections ||
        (type === "SphereGeometry" && mesh.material.name === "dark") ||
        /engine_shroud|drive_block|command_deck|cold_habitat|cold_shell|command_sensory|communal_dome|module_housing|freight_box|sealed_pod/.test(
          name,
        );
    if (!eligible) continue;
    if (type === "SphereGeometry") plateSphere(mesh, mats, lod);
    else if (type === "CylinderGeometry") plateCylinder(mesh, mats, lod);
    else if (type === "BoxGeometry") plateBox(mesh, mats, lod);
    else if (mesh.userData.loftSections) plateLoft(mesh, mats, lod);
  }
  root.userData.construction =
    "Segmented metallic plating, recessed machinery and structural interfaces; original mesh geometry";
  root.traverse((mesh) => {
    if (!mesh.isMesh) return;
    let g = mesh.geometry;
    if (mesh.userData.loftSections) {
      const flat = g.toNonIndexed();
      flat.computeVertexNormals();
      g.dispose();
      g = mesh.geometry = mergeVertices(flat, 1e-7);
      flat.dispose();
    }
    if (!mesh.material.normalMap) return;
    if (!g.attributes.uv) {
      g.computeBoundingBox();
      const box = g.boundingBox,
        extent = box.getSize(vector()),
        pos = g.attributes.position,
        uv = [];
      for (let i = 0; i < pos.count; i++)
        uv.push(
          (pos.getX(i) - box.min.x) / Math.max(extent.x, 0.0001),
          (pos.getZ(i) - box.min.z) / Math.max(extent.z, 0.0001),
        );
      g.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
    }
    if (!g.index)
      g.setIndex(
        Array.from({ length: g.attributes.position.count }, (_, i) => i),
      );
    g.computeTangents();
    const tangent = g.attributes.tangent,
      normal = g.attributes.normal;
    for (let i = 0; i < tangent.count; i++) {
      const t = vector(tangent.getX(i), tangent.getY(i), tangent.getZ(i));
      if (!Number.isFinite(t.lengthSq()) || t.lengthSq() < 0.5) {
        const n = vector().fromBufferAttribute(normal, i);
        t.copy(Math.abs(n.y) < 0.9 ? vector(0, 1, 0) : vector(1, 0, 0))
          .cross(n)
          .normalize();
        tangent.setXYZW(i, t.x, t.y, t.z, 1);
      }
    }
  });
}
