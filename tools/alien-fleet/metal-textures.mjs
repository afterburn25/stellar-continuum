import * as THREE from "three";
import { width, data } from "./metal-texture-data.mjs";
let textures;
export function getMetalTextures() {
  if (textures) return textures;
  textures = {};
  for (const [name, encoded] of Object.entries(data)) {
    const bytes = Uint8Array.from(atob(encoded), (c) => c.charCodeAt(0)),
      texture = new THREE.DataTexture(bytes, width, width, THREE.RGBAFormat);
    texture.name = `Hull metal ${name}`;
    texture.colorSpace =
      name === "albedo" ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
    texture.magFilter = THREE.LinearFilter;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.generateMipmaps = true;
    texture.anisotropy = 8;
    texture.needsUpdate = true;
    textures[name] = texture;
  }
  return textures;
}
