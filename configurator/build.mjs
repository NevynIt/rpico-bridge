import { build } from 'esbuild';
import { mkdir, readFile, writeFile } from 'node:fs/promises';

const bundledPackages = ['react', 'react-dom', 'scheduler'];
const licenseSections = await Promise.all(bundledPackages.map(async name => {
  const packageJson = JSON.parse(await readFile(`node_modules/${name}/package.json`, 'utf8'));
  const license = (await readFile(`node_modules/${name}/LICENSE`, 'utf8')).trim();
  if (packageJson.license !== 'MIT' || !license.includes('Permission is hereby granted')) {
    throw new Error(`Expected the full MIT license text for ${name}.`);
  }
  return `Bundled dependency ${name}@${packageJson.version} (MIT)\n${license}`;
}));
const licenseBody = licenseSections.join('\n\n').replace(/--/g, '- -');
if (licenseBody.includes('-->')) throw new Error('Dependency license comment is not safe to inline.');
const licenseComment = `<!--\n${licenseBody}\n-->`;

const output = await build({
  entryPoints: ['src/main.jsx'],
  bundle: true,
  write: false,
  minify: true,
  format: 'iife',
  target: ['es2022'],
  loader: { '.css': 'text' },
  legalComments: 'inline',
});
const template = await readFile('index.template.html', 'utf8');
const bundle = output.outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
const html = template
  .replace('<!-- APP_LICENSES -->', () => licenseComment)
  .replace('/* APP_BUNDLE */', () => bundle);
await mkdir('dist', { recursive: true });
await writeFile('dist/index.html', html);
console.log(`Created dist/index.html (${Buffer.byteLength(html)} bytes)`);
