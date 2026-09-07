import { Resvg } from '@resvg/resvg-js';
import pngToIco from 'png-to-ico';
import { PNG } from 'pngjs';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

export const directory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../assets/branding');

export async function renderAssets() {
  const source = await readFile(path.join(directory, 'velos-logo.svg'), 'utf8');
  const render = (svg, options = {}) => Buffer.from(new Resvg(svg, { font: { loadSystemFonts: false }, ...options }).render().asPng());
  const wordmark = render(source, { fitTo: { mode: 'height', value: 512 } });
  const full = PNG.sync.read(wordmark);
  const mark = new PNG({ width: 512, height: 512 });
  PNG.bitblt(full, mark, 0, 0, 512, 512, 0, 0);
  const markPng = PNG.sync.write(mark);
  const markData = `data:image/png;base64,${markPng.toString('base64')}`;
  const wordmarkData = `data:image/png;base64,${wordmark.toString('base64')}`;
  const iconSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <rect x="3" y="3" width="250" height="250" rx="28" fill="#192321"/>
  <image x="12" y="12" width="232" height="232" href="${markData}"/>
</svg>`;
  const icon = render(iconSvg);
  const splashSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720" viewBox="0 0 1280 720">
  <defs>
    <linearGradient id="background" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#1B2523"/><stop offset="1" stop-color="#111817"/></linearGradient>
    <pattern id="grid" width="64" height="64" patternUnits="userSpaceOnUse" patternTransform="skewX(-18)"><path d="M64 0H0V64" fill="none" stroke="#2D3C36" stroke-width="1"/></pattern>
    <linearGradient id="gridFade"><stop stop-color="white" stop-opacity="0"/><stop offset="1" stop-color="white" stop-opacity="0.65"/></linearGradient>
    <mask id="fade"><rect width="1280" height="720" fill="url(#gridFade)"/></mask>
  </defs>
  <rect width="1280" height="720" fill="url(#background)"/>
  <rect width="1280" height="720" fill="url(#grid)" mask="url(#fade)"/>
  <path d="M930 0L604 720M1168 0L842 720" stroke="#34493F" stroke-width="1" opacity="0.55"/>
  <path d="M64 64H102" stroke="#48DDB0" stroke-width="5"/>
  <path d="M110 64H127" stroke="#F18B73" stroke-width="5"/>
  <image x="190" y="196" width="900" height="256" href="${wordmarkData}"/>
  <path d="M64 625H1216" stroke="#35443E" stroke-width="2"/>
</svg>`;
  const darkLogoSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="1800" height="512"><rect width="1800" height="512" fill="#192321"/><image width="1800" height="512" href="${wordmarkData}"/></svg>`;
  return new Map([
    ['velos-wordmark.png', wordmark], ['velos-mark.png', markPng], ['velos-logo.png', render(darkLogoSvg)],
    ['velos-icon.png', icon], ['velos.ico', await pngToIco(icon)], ['velos-splash.png', render(splashSvg)]
  ]);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const assets = await renderAssets();
  await mkdir(directory, { recursive: true });
  for (const [name, content] of assets) { await writeFile(path.join(directory, name), content); }
  console.log(`Generated ${assets.size} Velos branding assets from the vector master.`);
}