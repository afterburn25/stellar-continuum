import { createCanvas, loadImage, ImageData } from "@napi-rs/canvas";
globalThis.ImageData = ImageData;
globalThis.self = globalThis;
globalThis.OffscreenCanvas = class {
  constructor(width, height) {
    this.canvas = createCanvas(width, height);
  }
  get width() {
    return this.canvas.width;
  }
  set width(n) {
    this.canvas.width = n;
  }
  get height() {
    return this.canvas.height;
  }
  set height(n) {
    this.canvas.height = n;
  }
  getContext(type, options) {
    return this.canvas.getContext(type, options);
  }
  async convertToBlob({ type = "image/png" } = {}) {
    return new Blob([this.canvas.toBuffer(type)], { type });
  }
};
globalThis.createImageBitmap = async (blob) => {
  const image = await loadImage(Buffer.from(await blob.arrayBuffer())),
    canvas = new OffscreenCanvas(image.width, image.height);
  canvas.getContext("2d").drawImage(image, 0, 0);
  return canvas;
};
globalThis.ProgressEvent = class {
  constructor(type, options = {}) {
    this.type = type;
    Object.assign(this, options);
  }
};
