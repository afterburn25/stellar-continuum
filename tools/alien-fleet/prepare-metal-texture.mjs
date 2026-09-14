import fs from "node:fs";
import path from "node:path";
import { createCanvas, loadImage, ImageData } from "@napi-rs/canvas";
const root = path.resolve("assets/models/alien-fleet-v2/Textures");
fs.mkdirSync(root, { recursive: true });
const source = path.join(root, "hull-metal-source.png"),
  image = await loadImage(source),
  n = 256,
  canvas = createCanvas(n, n),
  ctx = canvas.getContext("2d");
ctx.drawImage(image, 0, 0, n, n);
const data = ctx.getImageData(0, 0, n, n),
  orm = new Uint8ClampedArray(n * n * 4),
  normal = new Uint8ClampedArray(n * n * 4);
const gray = (x, y) => {
  const i = (((y + n) % n) * n + ((x + n) % n)) * 4;
  return (data.data[i] + data.data[i + 1] + data.data[i + 2]) / 765;
};
for (let y = 0; y < n; y++)
  for (let x = 0; x < n; x++) {
    const i = (y * n + x) * 4,
      g = gray(x, y);
    orm.set([255, Math.round(95 + g * 95), 242, 255], i);
    let nx = (gray(x - 1, y) - gray(x + 1, y)) * 0.55,
      ny = (gray(x, y - 1) - gray(x, y + 1)) * 0.55,
      nz = 1,
      d = Math.hypot(nx, ny, nz);
    normal.set(
      [
        Math.round(((nx / d) * 0.5 + 0.5) * 255),
        Math.round(((ny / d) * 0.5 + 0.5) * 255),
        Math.round(((nz / d) * 0.5 + 0.5) * 255),
        255,
      ],
      i,
    );
  }
fs.writeFileSync(
  path.join(root, "hull-metal-albedo.png"),
  canvas.toBuffer("image/png"),
);
for (const [name, bytes] of [
  ["orm", orm],
  ["normal", normal],
]) {
  ctx.putImageData(new ImageData(bytes, n, n), 0, 0);
  fs.writeFileSync(
    path.join(root, `hull-metal-${name}.png`),
    canvas.toBuffer("image/png"),
  );
}
const values = {
  albedo: Buffer.from(data.data).toString("base64"),
  orm: Buffer.from(orm).toString("base64"),
  normal: Buffer.from(normal).toString("base64"),
};
fs.writeFileSync(
  "tools/alien-fleet/metal-texture-data.mjs",
  "// Encoded runtime textures derived from the retained generated surface scan. Rebuild with prepare-metal-texture.mjs.\nexport const width=256;\nexport const data=" +
    JSON.stringify(values) +
    ";\n",
);
console.log(
  "Prepared 256 px albedo, roughness/metalness and shallow normal detail; original retained.",
);
