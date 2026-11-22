import { build } from 'esbuild';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
let outputDir = resolve(scriptDir, 'dist');
if (args.length) {
  if (args.length !== 2 || args[0] !== '--out') {
    throw new Error('Usage: node build.mjs [--out DIRECTORY]');
  }
  outputDir = resolve(args[1]);
}

const bundledPackages = ['react', 'react-dom', 'scheduler'];
const licenseSections = await Promise.all(bundledPackages.map(async name => {
  const packageJson = JSON.parse(await readFile(resolve(scriptDir, `node_modules/${name}/package.json`), 'utf8'));
  const license = (await readFile(resolve(scriptDir, `node_modules/${name}/LICENSE`), 'utf8')).trim();
  if (packageJson.license !== 'MIT' || !license.includes('Permission is hereby granted')) {
    throw new Error(`Expected the full MIT license text for ${name}.`);
  }
  return `Bundled dependency ${name}@${packageJson.version} (MIT)\n${license}`;
}));
const licenseBody = licenseSections.join('\n\n').replace(/--/g, '- -');
if (licenseBody.includes('-->')) throw new Error('Dependency license comment is not safe to inline.');
const licenseComment = `<!--\n${licenseBody}\n-->`;

const output = await build({
  entryPoints: [resolve(scriptDir, 'src/main.jsx')],
  bundle: true,
  write: false,
  minify: true,
  format: 'iife',
  target: ['es2022'],
  loader: { '.css': 'text' },
  legalComments: 'inline',
});
const template = await readFile(resolve(scriptDir, 'index.template.html'), 'utf8');
const bundle = output.outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
const html = template
  .replace('<!-- APP_LICENSES -->', () => licenseComment)
  .replace('/* APP_BUNDLE */', () => bundle);
await mkdir(outputDir, { recursive: true });
const outputPath = resolve(outputDir, 'index.html');
await writeFile(outputPath, html);
console.log(`Created ${outputPath} (${Buffer.byteLength(html)} bytes)`);
