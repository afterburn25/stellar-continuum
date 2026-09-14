import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { build } from "esbuild";
import { GLTFExporter } from "three/addons/exporters/GLTFExporter.js";
import * as THREE from "three";
import validator from "gltf-validator";
import { races, roles, ships, moduleTypes, checkFitting } from "./catalog.mjs";
import {
  createHull,
  createModule,
  presetFittings,
  applyFittings,
  clearFittings,
  disposeTree,
} from "./geometry.mjs";
const here = path.dirname(fileURLToPath(import.meta.url)),
  repo = path.resolve(here, "../.."),
  out = path.join(repo, "assets/models/alien-fleet-v1");
fs.mkdirSync(out, { recursive: true });
for (const d of ["Hulls", "Modules", "Renders"])
  fs.mkdirSync(path.join(out, d), { recursive: true });
// Three's exporter uses the browser FileReader API; Node supplies the same Blob bytes.
globalThis.FileReader = class {
  readAsArrayBuffer(blob) {
    blob
      .arrayBuffer()
      .then((result) => {
        this.result = result;
        this.onloadend?.();
      })
      .catch((e) => this.onerror?.(e));
  }
  readAsDataURL(blob) {
    blob
      .arrayBuffer()
      .then((result) => {
        this.result = `data:${blob.type};base64,${Buffer.from(result).toString("base64")}`;
        this.onloadend?.();
      })
      .catch((e) => this.onerror?.(e));
  }
};
const records = [],
  validation = [],
  shipRecords = [];
async function exportOne(root, relative) {
  const data = await new GLTFExporter().parseAsync(root, {
    binary: true,
    onlyVisible: true,
  });
  const buffer = Buffer.from(data);
  fs.writeFileSync(path.join(out, relative), buffer);
  const result = await validator.validateBytes(new Uint8Array(buffer), {
    uri: relative,
  });
  if (result.issues.numErrors)
    throw new Error(`${relative}: ${JSON.stringify(result.issues)}`);
  let triangles = 0,
    meshes = 0;
  root.traverse((o) => {
    if (o.isMesh) {
      meshes++;
      triangles +=
        (o.geometry.index
          ? o.geometry.index.count
          : o.geometry.attributes.position.count) / 3;
    }
  });
  records.push({
    path: relative.replaceAll("\\", "/"),
    bytes: buffer.length,
    sha256: createHash("sha256").update(buffer).digest("hex"),
    triangles,
    meshes,
  });
  validation.push({
    file: relative,
    errors: result.issues.numErrors,
    warnings: result.issues.numWarnings,
    messages: result.issues.messages,
  });
}
for (const ship of ships) {
  const name = `${ship.raceId}--${ship.roleId}`,
    hull = createHull(ship);
  const bounds = new THREE.Box3()
    .setFromObject(hull.root.getObjectByName("HULL_METRE_SCALE"))
    .getSize(new THREE.Vector3());
  for (const [axis, dim] of [
    ["x", "width"],
    ["y", "height"],
    ["z", "length"],
  ])
    if (Math.abs(bounds[axis] - ship.dimensions[dim]) > 0.01)
      throw new Error(`Size mismatch ${ship.id}`);
  const sockets = hull.sockets;
  if (new Set(sockets.map((s) => s.id)).size !== sockets.length)
    throw new Error("Duplicate ports");
  const sample = presetFittings(hull);
  if (!checkFitting(ship, sockets, sample).valid)
    throw new Error("Invalid sample");
  await exportOne(hull.root, `Hulls/${name}.glb`);
  const low = createHull(ship, { lod: 1 });
  await exportOne(low.root, `Hulls/${name}--lod1.glb`);
  disposeTree(low.root);
  applyFittings(hull, sample);
  let fittedCount = 0;
  for (const s of sockets) {
    const n = hull.root.getObjectByName(s.id);
    if (n.children.length) fittedCount++;
  }
  if (fittedCount !== Object.keys(sample).length)
    throw new Error("Missing fitted geometry");
  clearFittings(hull);
  if (sockets.some((s) => hull.root.getObjectByName(s.id).children.length))
    throw new Error("Removal failed");
  shipRecords.push({
    ...ship,
    hull: `Hulls/${name}.glb`,
    lod1: `Hulls/${name}--lod1.glb`,
    sockets,
    sampleFitting: sample,
  });
  disposeTree(hull.root);
}
for (const race of races)
  for (const m of moduleTypes) {
    const root = createModule(race.id, m.id);
    await exportOne(root, `Modules/${race.id}--${m.id}.glb`);
    disposeTree(root);
  }
const catalogue = {
  schemaVersion: 1,
  kind: "stellar-alien-fleet-asset-pack",
  version: "0.1.0",
  date: "2026-09-13",
  status: "library_candidate",
  units: "metres",
  axes: { up: "+Y", forward: "-Z", moduleOutward: "+Y" },
  races,
  roles,
  modules: moduleTypes,
  ships: shipRecords,
  files: records,
  integration: {
    godot: "pending",
    nativeCpp: "pending",
    researchBindings: "pending",
    damageStates: "pending",
    weaponArcsAndCollision: "pending",
  },
  sizePolicy:
    "Proposed exterior dimensions. Population reservations are preserved as literal passengers for carrier sizing; provisional volume allowances are not modeled interiors or engineering certification.",
};
fs.writeFileSync(
  path.join(out, "fleet-manifest.json"),
  JSON.stringify(catalogue, null, 2) + "\n",
);
fs.writeFileSync(
  path.join(out, "validation-report.json"),
  JSON.stringify(
    {
      glbs: validation.length,
      errors: validation.reduce((n, x) => n + x.errors, 0),
      warnings: validation.reduce((n, x) => n + x.warnings, 0),
      dimensionChecks: 18,
      attachmentAndRemovalChecks: 18,
      results: validation,
    },
    null,
    2,
  ) + "\n",
);
const bundle = await build({
  entryPoints: [path.join(here, "viewer.mjs")],
  bundle: true,
  minify: true,
  format: "iife",
  write: false,
  legalComments: "inline",
});
const html = fs
  .readFileSync(path.join(here, "workshop.html"), "utf8")
  .replace("/* BUNDLE */", () =>
    bundle.outputFiles[0].text.replaceAll("</script", "<\\/script"),
  )
  .replace(/[\t ]+$/gm, "");
fs.writeFileSync(path.join(out, "Alien-Fleet-Workshop.html"), html);
const licenses = ["three", "esbuild", "gltf-validator"]
  .map((name) => {
    const root = path.join(here, "node_modules", name);
    const file = ["LICENSE", "LICENSE.md", "LICENSE.txt"].find((f) =>
      fs.existsSync(path.join(root, f)),
    );
    const notices = path.join(root, "NOTICES");
    return `=== ${name} ===\n${file ? fs.readFileSync(path.join(root, file), "utf8") : "See package repository for license."}${fs.existsSync(notices) ? "\n\n" + fs.readFileSync(notices, "utf8") : ""}`;
  })
  .join("\n\n");
fs.writeFileSync(path.join(out, "THIRD-PARTY-LICENSES.txt"), licenses);
console.log(
  JSON.stringify(
    {
      hulls: 18,
      reducedDetailHulls: 18,
      modules: 36,
      glbs: records.length,
      totalBytes: records.reduce((n, f) => n + f.bytes, 0),
      validationErrors: 0,
      validationWarnings: validation.reduce((n, x) => n + x.warnings, 0),
      out,
    },
    null,
    2,
  ),
);
