import fs from "node:fs";
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import path from "node:path";
import { build } from "esbuild";
const require = createRequire(import.meta.url),
  out = path.resolve("assets/models/alien-fleet-v2");
const bundled = await build({
  entryPoints: ["tools/alien-fleet/viewer.mjs"],
  bundle: true,
  minify: true,
  format: "iife",
  write: false,
  legalComments: "inline",
});
fs.writeFileSync(
  path.join(out, "Alien-Fleet-Workshop.html"),
  fs
    .readFileSync("tools/alien-fleet/workshop.html", "utf8")
    .replace("/* BUNDLE */", () =>
      bundled.outputFiles[0].text.replaceAll("</script", "<\\/script"),
    )
    .replace(/[\t ]+$/gm, ""),
);
const {
  chromium,
} = require("C:/Users/after/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");
const browser = await chromium.launch({
    headless: true,
    executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe",
  }),
  page = await browser.newPage({ viewport: { width: 1400, height: 850 } });
page.on("pageerror", (e) => console.log(e.message));
await page.goto(
  pathToFileURL(path.join(out, "Alien-Fleet-Workshop.html")).href,
);
await page.waitForFunction(() => fleetWorkshop.ready);
await page.addStyleTag({
  content:
    "header,.racebar,aside,footer,.model-title,.viewbar,.viewport-footer{display:none!important}.layout{display:block;height:100vh;min-height:0}.viewport{width:100vw;height:100vh}",
});
for (const race of [
  "pelagic_high_pressure",
  "compact_high_gravity",
  "cryogenic_hydrocarbon",
]) {
  await page.evaluate((r) => {
    fleetWorkshop.selectShip(r, "patrol_corvette");
    document.getElementById("preset").click();
    fleetWorkshop.setPorts(false);
    fleetWorkshop.setShadows(false);
  }, race);
  await page.evaluate(async () => {
    for (let i = 0; i < 10; i++) await new Promise(requestAnimationFrame);
    fleetWorkshop.render();
  });
  await page.screenshot({
    path: path.join(out, "Renders", race + "--metal-inspection.png"),
  });
}
await browser.close();
