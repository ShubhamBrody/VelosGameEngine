import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { PNG } from 'pngjs';

const directory = process.argv[2];
if (!directory) { throw new Error('Provide a branding smoke capture directory.'); }
const load = async name => PNG.sync.read(await readFile(path.join(directory, name)));
for (const name of ['splash-96.png', 'splash-144.png', 'splash-192.png', 'splash-288.png', 'editor-splash.png', 'runtime-splash.png', 'export-splash.png']) {
  const image = await load(name);
  assert.equal(image.width * 9, image.height * 16, `${name} must preserve the splash aspect ratio.`);
  assert.ok(image.width >= 320 && image.height >= 180, `${name} must remain a usable size.`);
  let mint = 0;
  let coral = 0;
  let wordmark = 0;
  for (let row = Math.floor(image.height * 0.28); row < image.height * 0.62; row++) {
    for (let column = Math.floor(image.width * 0.13); column < image.width * 0.86; column++) {
      const offset = (row * image.width + column) * 4;
      const [red, green, blue] = image.data.subarray(offset, offset + 3);
      if (green > red + 50 && blue > red + 30) { mint++; }
      if (red > green + 40 && red > blue + 60) { coral++; }
      if (red > 210 && green > 210 && blue > 210) { wordmark++; }
    }
  }
  assert.ok(mint > 100 && coral > 50 && wordmark > 100, `${name} must contain visible logo artwork, not just a background.`);
}
const shown = await load('runtime-ready.png');
const skipped = await load('runtime-no-splash.png');
const exported = await load('export-ready.png');
for (const [name, image] of [['disabled splash', skipped], ['exported runtime', exported]]) {
  assert.equal(image.width, shown.width, `${name} dimensions`);
  assert.equal(image.height, shown.height, `${name} dimensions`);
  assert.deepEqual(image.data, shown.data, `${name} must render the same game pixels.`);
}
console.log('PASS: native splash pixels at four DPI sizes, automatic/explicit startup policy and exact runtime/export image equivalence.');