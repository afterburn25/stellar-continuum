import fs from "node:fs";
import "./node-image-api.mjs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath, pathToFileURL } from "node:url";
import { createRequire } from "node:module";
import validator from "gltf-validator";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import * as THREE from "three";
import { races, roles, ships, moduleTypes, checkFitting } from "./catalog.mjs";
const require = createRequire(import.meta.url),
  here = path.dirname(fileURLToPath(import.meta.url)),
  out = path.resolve(here, "../../assets/models/alien-fleet-v2"),
  manifest = JSON.parse(fs.readFileSync(path.join(out, "fleet-manifest.json"))),
  checks = [];
function pass(name) {
  checks.push(name);
}
function parseGLB(file) {
  const b = fs.readFileSync(file);
  return new Promise((resolve, reject) =>
    new GLTFLoader().parse(
      b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength),
      "",
      resolve,
      reject,
    ),
  );
}
for (const s of manifest.ships) {
  const gltf = await parseGLB(path.join(out, s.hull)),
    low = await parseGLB(path.join(out, s.lod1));
  const bounds = new THREE.Box3()
    .setFromObject(gltf.scene.getObjectByName("HULL_METRE_SCALE"))
    .getSize(new THREE.Vector3());
  for (const [axis, dim] of [
    ["x", "width"],
    ["y", "height"],
    ["z", "length"],
  ])
    assert(
      Math.abs(bounds[axis] - s.dimensions[dim]) < 0.02,
      `${s.name} ${dim} roundtrip`,
    );
  for (const socket of s.sockets) {
    const a = gltf.scene.getObjectByName(socket.id),
      b = low.scene.getObjectByName(socket.id);
    assert(a && b);
    assert.equal(a.userData.kind, socket.kind);
    assert.equal(a.children.length, 0);
    assert(
      a.position.distanceTo(b.position) < 1e-6,
      "LOD sockets must stay fixed",
    );
    assert(a.position.distanceTo(new THREE.Vector3(...socket.position)) < 1e-6);
  }
  pass(
    `${s.name}: independent GLB import, metre dimensions, empty hardpoints and identical LOD sockets`,
  );
}
const { chromium } = require(
  process.env.PLAYWRIGHT_MODULE ||
    "C:/Users/after/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright",
);
const executablePath = [
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
].find(fs.existsSync);
const browser = await chromium.launch({
  headless: true,
  ...(executablePath ? { executablePath } : {}),
});
const page = await browser.newPage({
  viewport: { width: 1600, height: 1000 },
  deviceScaleFactor: 1,
});
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
await page.goto(
  pathToFileURL(path.join(out, "Alien-Fleet-Workshop.html")).href,
);
await page.waitForFunction(() => window.fleetWorkshop?.ready);
const settle = () =>
  page.evaluate(async () => {
    for (let i = 0; i < 12; i++) await new Promise(requestAnimationFrame);
    window.fleetWorkshop.render();
  });
for (const race of races) {
  await page.locator(`[data-race="${race.id}"]`).click();
  await page.locator('[data-role="patrol_corvette"]').click();
  const bare = await page.evaluate(() => fleetWorkshop.getState());
  await page.locator("#module").selectOption("beam_turret");
  await page.locator("#fit").click();
  let state = await page.evaluate(() => fleetWorkshop.getState());
  assert.equal(state.fittings.HP_W01, "beam_turret");
  assert(state.meshCount > bare.meshCount);
  await page.locator("#module").selectOption("kinetic_turret");
  await page.locator("#fit").click();
  state = await page.evaluate(() => fleetWorkshop.getState());
  assert.equal(state.fittings.HP_W01, "kinetic_turret");
  await page.locator("#remove").click();
  assert.equal(
    (await page.evaluate(() => fleetWorkshop.getState())).meshCount,
    bare.meshCount,
  );
  await page.locator("#preset").click();
  state = await page.evaluate(() => fleetWorkshop.getState());
  assert(state.budget.valid && Object.keys(state.fittings).length > 3);
  await page.locator("#focus").click();
  await settle();
  await page.locator("#frame").click();
  pass(
    `${race.short}: real UI fitting, weapon replacement, removal, preset and close-up`,
  );
}
const before = await page.evaluate(() => fleetWorkshop.blueprint());
const accepted = await page.evaluate(() =>
  fleetWorkshop.setFittings({ HP_U01: "kinetic_turret" }),
);
assert.equal(accepted, false);
assert.deepEqual(await page.evaluate(() => fleetWorkshop.blueprint()), before);
pass("Incompatible slot rejected without changing the blueprint");
const over = Object.fromEntries(
  (await page.evaluate(() => fleetWorkshop.getState())).sockets.map((s) => [
    s.id,
    {
      weapon: "missile_pod",
      utility: "sensor_array",
      defense: "shield_emitter",
      thermal: "power_unit",
    }[s.kind],
  ]),
);
assert.equal(
  await page.evaluate((x) => fleetWorkshop.setFittings(x), over),
  false,
);
pass("Over-budget loadout rejected");
const downloadPromise = page.waitForEvent("download");
await page.locator("#save").click();
const download = await downloadPromise,
  blueprintFile = path.join(out, "example-blueprint.json");
await download.saveAs(blueprintFile);
const saved = JSON.parse(fs.readFileSync(blueprintFile, "utf8"));
await page.locator("#bare").click();
await page.locator("#load-file").setInputFiles(blueprintFile);
await page.waitForFunction(() =>
  document.getElementById("notice").textContent.includes("Blueprint loaded"),
);
assert.deepEqual(
  (await page.evaluate(() => fleetWorkshop.blueprint())).fittings,
  saved.fittings,
);
pass("Blueprint download and upload roundtrip");
const glbDownload = page.waitForEvent("download");
await page.locator("#export").click();
const exported = await glbDownload,
  exportFile = path.join(out, "example-fitted-corvette.glb");
await exported.saveAs(exportFile);
const result = await validator.validateBytes(
  new Uint8Array(fs.readFileSync(exportFile)),
);
assert.equal(result.issues.numErrors, 0);
const fitted = await parseGLB(exportFile);
for (const socket of Object.keys(saved.fittings))
  assert(fitted.scene.getObjectByName(socket).children.length === 1);
pass("Fitted GLB export validates and re-imports with equipment attached");
const renderPage = await browser.newPage({
  viewport: { width: 1200, height: 640 },
  deviceScaleFactor: 1,
});
await renderPage.goto(
  pathToFileURL(path.join(out, "Alien-Fleet-Workshop.html")).href,
);
await renderPage.waitForFunction(() => window.fleetWorkshop?.ready);
await renderPage.addStyleTag({
  content:
    "header,.racebar,aside,footer,.model-title,.viewbar,.viewport-footer{display:none!important}.layout{display:block;height:100vh;min-height:0}.viewport{width:100vw;height:100vh}",
});
const pictures = [];
for (const ship of ships) {
  await renderPage.evaluate(
    ({ raceId, roleId }) => fleetWorkshop.selectShip(raceId, roleId),
    ship,
  );
  await renderPage.evaluate(() => {
    document.getElementById("preset").click();
    fleetWorkshop.setPorts(false);
  });
  await renderPage.evaluate(async () => {
    for (let i = 0; i < 12; i++) await new Promise(requestAnimationFrame);
    fleetWorkshop.render();
  });
  const file = `${ship.raceId}--${ship.roleId}.png`;
  await renderPage
    .locator("#viewport")
    .screenshot({ path: path.join(out, "Renders", file) });
  pictures.push({ ship, file });
  pass(`${ship.name}: workshop model render`);
}
await page.evaluate(() =>
  fleetWorkshop.selectShip("pelagic_high_pressure", "patrol_corvette"),
);
await page.locator("#preset").click();
await settle();
await page.screenshot({
  path: path.join(out, "Renders", "workshop-overview.png"),
});
await page.locator("#bare").click();
await page.evaluate(() => fleetWorkshop.setPorts(false));
await settle();
await page
  .locator("#viewport")
  .screenshot({ path: path.join(out, "Renders", "fitting-before.png") });
await page.locator("#preset").click();
await settle();
await page
  .locator("#viewport")
  .screenshot({ path: path.join(out, "Renders", "fitting-after.png") });
await page.setViewportSize({ width: 390, height: 844 });
await settle();
assert(
  await page.evaluate(
    () => document.documentElement.scrollWidth <= innerWidth + 1,
  ),
  "Mobile layout overflow",
);
await page.screenshot({
  path: path.join(out, "Renders", "workshop-mobile.png"),
  fullPage: true,
});
pass("390 px responsive layout without horizontal overflow");
assert.deepEqual(errors, []);
pass("No browser runtime errors");
const sheet = await browser.newPage({
  viewport: { width: 2100, height: 2380 },
});
const imageData = (file) =>
  "data:image/png;base64," +
  fs.readFileSync(path.join(out, "Renders", file)).toString("base64");
const cards = roles
  .map((role) =>
    races.map((race) =>
      pictures.find(
        (p) => p.ship.roleId === role.id && p.ship.raceId === race.id,
      ),
    ),
  )
  .flat();
await sheet.setContent(
  `<html><head><style>*{box-sizing:border-box}body{margin:0;background:#070e17;color:#e4edf5;font:14px 'Segoe UI',sans-serif;padding:42px}.eyebrow{color:#75d8d0;letter-spacing:5px;font-size:13px}h1{font-size:45px;font-weight:400;margin:10px 0}.intro{color:#9cabbc;font-size:18px;margin-bottom:24px}.columns,.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:18px}.columns{font-size:20px;margin:22px 0 16px}.columns span{border-top:2px solid;padding-top:13px}.card{background:#0b1723;border:1px solid #293d4d;overflow:hidden;border-radius:8px;height:332px}.card img{width:100%;height:274px;object-fit:contain;object-position:50% 50%}.caption{padding:5px 17px;color:#94a9bb;display:flex;justify-content:space-between;font-size:13px}.caption b{font-weight:500;color:#dce8f3;font-size:16px}footer{margin-top:23px;color:#839aaf;font-size:15px;line-height:1.8}</style></head><body><div class="eyebrow">STELLAR CONTINUUM / METAL CONSTRUCTION 02</div><h1>Three species. Three ways to build a fleet.</h1><div class="intro">18 original 3D hulls · Detachable weapons and modules · Species-specific proportions</div><div class="columns">${races.map((r) => `<span style="color:${r.color}">${r.title}</span>`).join("")}</div><div class="grid">${cards.map(({ ship: s, file }) => `<div class="card"><img src="${imageData(file)}"><div class="caption"><b>${s.name}</b><span>${roles.find((r) => r.id === s.roleId).name} · ${s.dimensions.length >= 1000 ? s.dimensions.length / 1000 + " km" : s.dimensions.length + " m"}</span></div></div>`).join("")}</div><footer>Original model renders · Each ship framed independently; images are not at a common scale.<br>Asset candidates for the engine library. Game integration, interiors, weapon arcs and research bindings remain pending.</footer></body></html>`,
);
await sheet.screenshot({
  path: path.join(out, "Renders", "alien-fleet-collection.png"),
  fullPage: true,
});
assert.deepEqual(errors, []);
fs.writeFileSync(
  path.join(out, "workshop-test-report.json"),
  JSON.stringify(
    { passed: checks.length, failed: 0, browserErrors: errors, checks },
    null,
    2,
  ) + "\n",
);
console.log(
  JSON.stringify(
    { passed: checks.length, failed: 0, renders: pictures.length, errors },
    null,
    2,
  ),
);
await browser.close();
