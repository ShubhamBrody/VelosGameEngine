import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { PNG } from 'pngjs';
import { directory, renderAssets } from './generate.mjs';

test('brand assets are reproducible from the path-only vector master', async () => {
  const source = await readFile(path.join(directory, 'velos-logo.svg'), 'utf8');
  assert.ok(!/<(?:text|image)\b/.test(source), 'The master must not require fonts or external artwork.');
  for (const [name, content] of await renderAssets()) {
    assert.deepEqual(await readFile(path.join(directory, name)), content, `${name} must match the current source.`);
  }
});

test('logo, splash and icon have real visible pixels at their expected dimensions', async () => {
  for (const [name, width, height] of [['velos-mark.png',512,512],['velos-wordmark.png',1800,512],['velos-icon.png',256,256],['velos-splash.png',1280,720]]) {
    const image = PNG.sync.read(await readFile(path.join(directory, name)));
    assert.equal(image.width, width);
    assert.equal(image.height, height);
    let mint = 0;
    let coral = 0;
    let light = 0;
    let transparent = 0;
    for (let offset = 0; offset < image.data.length; offset += 4) {
      const [red, green, blue, alpha] = image.data.subarray(offset, offset + 4);
      if (alpha === 0) { transparent++; continue; }
      if (green > red + 50 && blue > red + 30) { mint++; }
      if (red > green + 40 && red > blue + 60) { coral++; }
      if (red > 210 && green > 210 && blue > 210) { light++; }
    }
    assert.ok(mint > 100 && coral > 100 && light > 100, `${name} must contain the actual three-color logo.`);
    if (name === 'velos-mark.png') { assert.ok(transparent > width * height / 2, 'The standalone mark must retain transparency.'); }
  }
});

test('Windows ICO includes bounded small and large icon images', async () => {
  const icon = await readFile(path.join(directory, 'velos.ico'));
  assert.equal(icon.readUInt16LE(0), 0);
  assert.equal(icon.readUInt16LE(2), 1);
  const count = icon.readUInt16LE(4);
  assert.ok(count >= 4);
  const sizes = [];
  for (let index = 0; index < count; ++index) {
    const offset = 6 + index * 16;
    const width = icon[offset] || 256;
    const height = icon[offset + 1] || 256;
    const bytes = icon.readUInt32LE(offset + 8);
    const location = icon.readUInt32LE(offset + 12);
    assert.equal(width, height);
    assert.ok(location >= 6 + count * 16 && location + bytes <= icon.length);
    sizes.push(width);
  }
  assert.ok(sizes.includes(16) && sizes.includes(32) && sizes.includes(256));
});