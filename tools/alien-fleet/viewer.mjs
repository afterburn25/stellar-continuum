import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { GLTFExporter } from "three/addons/exporters/GLTFExporter.js";
import {
  races,
  roles,
  ships,
  moduleTypes,
  getRace,
  getRole,
  getShip,
  getModule,
  compatible,
  checkFitting,
  formatLength,
  sizeMeters,
} from "./catalog.mjs";
import {
  createHull,
  applyFittings,
  presetFittings,
  disposeTree,
} from "./geometry.mjs";
const $ = (id) => document.getElementById(id),
  canvas = $("canvas");
const renderer = new THREE.WebGLRenderer({
  canvas,
  antialias: true,
  alpha: true,
  preserveDrawingBuffer: true,
});
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.3;
const scene = new THREE.Scene(),
  camera = new THREE.PerspectiveCamera(36, 1, 0.1, 1000000),
  controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.1;
scene.add(new THREE.HemisphereLight("#cce6ff", "#344252", 2.6));
for (const [color, power, pos] of [
  ["#e0f3ff", 4, [-3, 5, -4]],
  ["#7bb4e9", 2, [4, 1, 2]],
  ["#ffe3bf", 2, [0, -1, 3]],
]) {
  const light = new THREE.DirectionalLight(color, power);
  light.position.set(...pos);
  scene.add(light);
}
const kinds = {
  weapon: "#ffb27c",
  utility: "#73c8ff",
  defense: "#b8a2ff",
  cargo: "#7bd8ae",
  thermal: "#f2d982",
};
let hull,
  markers = new THREE.Group(),
  grid,
  fittings = {},
  selected,
  portsVisible = true,
  raceId = races[0].id,
  roleId = roles[0].id,
  focusMode = false;
scene.add(markers);
function resize() {
  const r = $("viewport").getBoundingClientRect();
  const previousAspect = camera.aspect;
  renderer.setSize(r.width, r.height, false);
  camera.aspect = r.width / r.height;
  camera.updateProjectionMatrix();
  if (hull && !focusMode && Math.abs(previousAspect - camera.aspect) > 0.01) {
    const activeView = document.querySelector(
      '[data-view][aria-pressed="true"]',
    );
    frame(activeView?.dataset.view || "iso");
  }
}
new ResizeObserver(resize).observe($("viewport"));
function status(text, error = false) {
  $("notice").textContent = text;
  $("notice").classList.toggle("error", error);
}
function updateNav() {
  const race = getRace(raceId);
  $("races").innerHTML =
    races
      .map(
        (r) =>
          `<button class="race ${r.id === raceId ? "active" : ""}" data-race="${r.id}">${r.title}<small>06 HULLS / ${r.short.toUpperCase()} DESIGN FAMILY</small></button>`,
      )
      .join("") +
    '<span class="count">18 original hulls<br>36 matching equipment models</span>';
  $("roles").innerHTML = roles
    .map(
      (r, i) =>
        `<button class="role ${r.id === roleId ? "active" : ""}" data-role="${r.id}"><div>${getShip(raceId, r.id).name}<span>${r.name}</span></div><div class="idx">0${i + 1}</div></button>`,
    )
    .join("");
  document.documentElement.style.setProperty("--accent", race.color);
  $("biology").textContent = race.biology;
  $("clearance").textContent = race.clearance;
  $("language").textContent = race.language;
}
function makeMarkers() {
  scene.remove(markers);
  disposeTree(markers);
  markers = new THREE.Group();
  markers.name = "EDITOR_MARKERS";
  const s = hull.ship;
  for (const port of hull.sockets) {
    const g = new THREE.Group();
    g.position.fromArray(port.position);
    g.quaternion.fromArray(port.rotationQuaternion);
    g.userData.socketId = port.id;
    const radius = Math.max(s.dimensions.length * 0.004, 1.2);
    const m = new THREE.Mesh(
      new THREE.SphereGeometry(radius, 12, 8),
      new THREE.MeshBasicMaterial({
        color: kinds[port.kind],
        transparent: true,
        opacity: 0.85,
        depthTest: false,
      }),
    );
    m.renderOrder = 10;
    m.userData.socketId = port.id;
    g.add(m);
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(radius * 2.0, radius * 0.15, 6, 24),
      new THREE.MeshBasicMaterial({ color: "#ffffff", depthTest: false }),
    );
    ring.rotation.x = Math.PI / 2;
    ring.renderOrder = 11;
    ring.name = "selection";
    ring.visible = port.id === selected;
    g.add(ring);
    markers.add(g);
  }
  markers.visible = portsVisible;
  scene.add(markers);
}
function frame(view = "iso") {
  focusMode = false;
  const { length: L, width: W, height: H } = hull.ship.dimensions;
  controls.target.set(0, 0, 0);
  const dist = Math.max(L, W, H) * Math.max(1.65, 1.1 / camera.aspect);
  const direction = {
    iso: [1.1, 0.87, -1.35],
    top: [0, 1, 0.0001],
    side: [1, 0.05, 0],
    front: [0, 0.04, -1],
  }[view];
  camera.up.set(0, 1, 0);
  camera.position.fromArray(direction).normalize().multiplyScalar(dist);
  camera.near = Math.max(0.05, L / 10000);
  camera.far = L * 30;
  camera.updateProjectionMatrix();
  controls.minDistance = 2;
  controls.maxDistance = L * 8;
  controls.update();
  document
    .querySelectorAll("[data-view]")
    .forEach((b) =>
      b.setAttribute("aria-pressed", String(b.dataset.view === view)),
    );
  updateMarkerScale();
}
function updateMarkerScale() {
  for (const m of markers.children)
    m.scale.setScalar(
      focusMode ? Math.min(1, 35 / hull.ship.dimensions.length) : 1,
    );
}
function selectShip(race, role) {
  raceId = race;
  roleId = role;
  if (hull) {
    scene.remove(hull.root);
    disposeTree(hull.root);
  }
  hull = createHull(getShip(race, role));
  fittings = {};
  selected = hull.sockets[0].id;
  scene.add(hull.root);
  if (grid) {
    scene.remove(grid);
    disposeTree(grid);
  }
  grid = new THREE.GridHelper(
    hull.ship.dimensions.length * 1.65,
    22,
    "#345263",
    "#1c3548",
  );
  grid.position.y = -hull.ship.dimensions.height * 0.61;
  grid.material.transparent = true;
  grid.material.opacity = 0.4;
  scene.add(grid);
  updateNav();
  const s = hull.ship;
  $("ship-role").textContent = `${getRace(race).short} / ${getRole(role).name}`;
  $("ship-name").textContent = s.name;
  $("ship-tagline").textContent = getRace(race).tagline;
  for (const d of ["length", "width", "height"])
    $(d).textContent = formatLength(s.dimensions[d]);
  $("port-count").textContent = String(hull.sockets.length).padStart(2, "0");
  $("scale-label").textContent = "Hull dimensions exclude attached equipment";
  $("payload").textContent =
    `${s.crew} crew${s.populationReservation ? " · " + new Intl.NumberFormat("en").format(s.populationReservation) + " colonists reserved in game" : ""}`;
  $("legend").innerHTML = Object.entries(kinds)
    .map(
      ([k, c]) =>
        `<span><i class="dot" style="background:${c}"></i>${k[0].toUpperCase() + k.slice(1)}</span>`,
    )
    .join("");
  makeMarkers();
  refresh();
  resize();
  frame();
  status(
    s.populationReservation
      ? "Large carrier dimensions are provisional: the existing game reserves millions of colonists. Interiors and transport balance still need review."
      : "Select a hardpoint and attach equipment to change the ship’s silhouette. Use Role loadout for a starting example.",
  );
}
function refresh() {
  const current = selected;
  $("socket").innerHTML = hull.sockets
    .map(
      (s) =>
        `<option value="${s.id}">${s.id} · ${s.kind}${fittings[s.id] ? " · fitted" : ""}</option>`,
    )
    .join("");
  $("socket").value = current;
  const t = checkFitting(hull.ship, hull.sockets, fittings);
  $("points").textContent = `${t.points} / ${hull.ship.points}`;
  $("power").textContent = `${t.power} / ${t.powerBudget}`;
  $("points-bar").style.width = `${(t.points / hull.ship.points) * 100}%`;
  $("power-bar").style.width = `${(t.power / t.powerBudget) * 100}%`;
  const count = Object.keys(fittings).length;
  $("installed").innerHTML =
    `<b>${count} / ${hull.sockets.length} ports occupied</b><br>${
      Object.entries(fittings)
        .map(([s, m]) => `${s}: ${getModule(m).name}`)
        .join("<br>") || "Bare hull · All optional equipment removed"
    }`;
  selectSocket(current);
}
function selectSocket(id) {
  selected = id;
  $("socket").value = id;
  const s = hull.sockets.find((x) => x.id === id);
  $("socket-kind").textContent = s.kind.toUpperCase();
  $("socket-size").textContent =
    `SIZE ${s.size} · ${s.footprintMeters} m mount`;
  $("module").innerHTML = moduleTypes
    .filter((m) => compatible(s, m))
    .map((m) => `<option value="${m.id}">${m.name} · ${m.size}</option>`)
    .join("");
  if (fittings[id]) $("module").value = fittings[id];
  for (const mark of markers.children)
    mark.getObjectByName("selection").visible = mark.userData.socketId === id;
  $("remove").disabled = !fittings[id];
  updateModule();
}
function updateModule() {
  const m = getModule($("module").value);
  $("module-desc").textContent = m.description;
  $("module-cost").textContent =
    `${m.points} allocation points · ${m.power} power${m.powerSupply ? " · +" + m.powerSupply + " supply" : ""}`;
  const check = checkFitting(hull.ship, hull.sockets, {
    ...fittings,
    [selected]: m.id,
  });
  $("fit").disabled = !check.valid;
  $("fit").textContent = check.valid
    ? fittings[selected]
      ? "Replace equipment"
      : "Attach equipment"
    : "Insufficient fitting budget";
}
function setFittings(value) {
  const result = checkFitting(hull.ship, hull.sockets, value);
  if (!result.valid) {
    status(result.errors.join(". "), true);
    return false;
  }
  applyFittings(hull, value);
  fittings = { ...value };
  refresh();
  return true;
}
function focusPort() {
  const s = hull.sockets.find((x) => x.id === selected),
    center = new THREE.Vector3().fromArray(s.position),
    out = new THREE.Vector3(0, 1, 0).applyQuaternion(
      new THREE.Quaternion().fromArray(s.rotationQuaternion),
    );
  const model = getModule(fittings[selected] || $("module").value),
    d = Math.max(sizeMeters[model.size] * 5, 35);
  controls.target.copy(center);
  camera.position
    .copy(center)
    .addScaledVector(out, d * 0.65)
    .add(new THREE.Vector3(d * 0.65, d * 0.12, -d * 0.85));
  camera.near = 0.05;
  camera.updateProjectionMatrix();
  controls.update();
  focusMode = true;
  updateMarkerScale();
  status(
    `${s.id} close-up · Equipment stays the same physical size on every hull. Whole ship restores the overview.`,
  );
}
function download(blob, name) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
function blueprint() {
  return {
    schemaVersion: 1,
    kind: "stellar-alien-fleet-blueprint",
    assetId: hull.ship.id,
    raceId,
    roleId,
    units: "metres",
    fittings: { ...fittings },
    gameIntegration: "pending",
  };
}
async function exportGLB() {
  try {
    $("export").disabled = true;
    const data = await new GLTFExporter().parseAsync(hull.root, {
      binary: true,
      onlyVisible: true,
    });
    download(
      new Blob([data], { type: "model/gltf-binary" }),
      `${hull.ship.name.replaceAll(" ", "-")}-fitted.glb`,
    );
    status(
      "Fitted model exported with separate equipment and named attachment nodes.",
    );
  } catch (e) {
    status(e.message, true);
  } finally {
    $("export").disabled = false;
  }
}
$("races").onclick = (e) => {
  const b = e.target.closest("[data-race]");
  if (b) selectShip(b.dataset.race, roleId);
};
$("roles").onclick = (e) => {
  const b = e.target.closest("[data-role]");
  if (b) selectShip(raceId, b.dataset.role);
};
$("socket").onchange = () => selectSocket($("socket").value);
$("module").onchange = updateModule;
$("fit").onclick = () => {
  if (setFittings({ ...fittings, [selected]: $("module").value }))
    status(`${getModule(fittings[selected]).name} attached to ${selected}.`);
};
$("remove").onclick = () => {
  const next = { ...fittings };
  delete next[selected];
  if (setFittings(next)) status(`Equipment removed from ${selected}.`);
};
$("bare").onclick = () => {
  setFittings({});
  status(
    "Bare hull restored. All optional weapons, shields, cargo and support modules are removed.",
  );
};
$("preset").onclick = () => {
  setFittings(presetFittings(hull));
  status(
    "Example loadout fitted within the proposed point and power budgets. Swap individual modules to customize it.",
  );
};
$("focus").onclick = focusPort;
$("frame").onclick = () => frame();
document
  .querySelectorAll("[data-view]")
  .forEach((b) => (b.onclick = () => frame(b.dataset.view)));
$("show-ports").onclick = () => {
  portsVisible = !portsVisible;
  markers.visible = portsVisible;
  $("show-ports").setAttribute("aria-pressed", String(portsVisible));
};
$("save").onclick = () =>
  download(
    new Blob([JSON.stringify(blueprint(), null, 2)], {
      type: "application/json",
    }),
    `${hull.ship.name.replaceAll(" ", "-")}-blueprint.json`,
  );
$("load").onclick = () => $("load-file").click();
$("load-file").onchange = async () => {
  try {
    const file = $("load-file").files[0];
    if (!file) return;
    if (file.size > 100000) throw new Error("Blueprint is too large.");
    const d = JSON.parse(await file.text());
    if (
      d.schemaVersion !== 1 ||
      d.kind !== "stellar-alien-fleet-blueprint" ||
      !getShip(d.raceId, d.roleId) ||
      getShip(d.raceId, d.roleId).id !== d.assetId ||
      !d.fittings ||
      typeof d.fittings !== "object" ||
      Array.isArray(d.fittings)
    )
      throw new Error("Unsupported blueprint.");
    const candidate = createHull(getShip(d.raceId, d.roleId));
    const check = checkFitting(candidate.ship, candidate.sockets, d.fittings);
    disposeTree(candidate.root);
    if (!check.valid) throw new Error(check.errors.join(". "));
    selectShip(d.raceId, d.roleId);
    setFittings(d.fittings);
    status("Blueprint loaded and fitting rules checked.");
  } catch (e) {
    status(e.message, true);
  } finally {
    $("load-file").value = "";
  }
};
$("export").onclick = exportGLB;
let down;
canvas.addEventListener("pointerdown", (e) => (down = [e.clientX, e.clientY]));
canvas.addEventListener("pointerup", (e) => {
  if (
    !portsVisible ||
    !down ||
    Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 5
  )
    return;
  const r = canvas.getBoundingClientRect(),
    pointer = new THREE.Vector2(
      ((e.clientX - r.left) / r.width) * 2 - 1,
      (-(e.clientY - r.top) / r.height) * 2 + 1,
    ),
    ray = new THREE.Raycaster();
  ray.setFromCamera(pointer, camera);
  const hit = ray
    .intersectObject(markers, true)
    .find((x) => x.object.userData.socketId);
  if (hit) selectSocket(hit.object.userData.socketId);
});
selectShip(raceId, roleId);
function render() {
  controls.update();
  renderer.render(scene, camera);
  requestAnimationFrame(render);
}
render();
// Small deterministic automation surface for validation and image production.
window.fleetWorkshop = {
  ready: true,
  selectShip,
  setFittings,
  frame,
  focusPort,
  selectSocket,
  blueprint,
  getState: () => ({
    ship: hull.ship,
    sockets: hull.sockets,
    fittings: { ...fittings },
    budget: checkFitting(hull.ship, hull.sockets, fittings),
    meshCount: (() => {
      let n = 0;
      hull.root.traverse((o) => {
        if (o.isMesh) n++;
      });
      return n;
    })(),
  }),
  setPorts: (visible) => {
    portsVisible = visible;
    markers.visible = visible;
    $("show-ports").setAttribute("aria-pressed", String(visible));
  },
  render: () => renderer.render(scene, camera),
};
