// Sets package.json (and package-lock.json) to the version in rg_converters/version.py.
// With --check, only fails when they differ.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';

const versionPy = readFileSync(new URL('../../rg_converters/version.py', import.meta.url), 'utf8');
const version = /^__version__ = "([^"]+)"/m.exec(versionPy)?.[1];
if (!version) {
    console.error('could not read __version__ from rg_converters/version.py');
    process.exit(1);
}

const files = [new URL('../package.json', import.meta.url), new URL('../package-lock.json', import.meta.url)];
const pkg = JSON.parse(readFileSync(files[0], 'utf8'));
if (pkg.version === version) process.exit(0);
if (process.argv.includes('--check')) {
    console.error(`package.json is ${pkg.version} but rg_converters/version.py is ${version}; run npm run sync-version`);
    process.exit(1);
}

for (const file of files.filter(existsSync)) {
    const json = JSON.parse(readFileSync(file, 'utf8'));
    json.version = version;
    if (json.packages?.['']) json.packages[''].version = version;
    writeFileSync(file, `${JSON.stringify(json, null, 2)}\n`);
}
console.log(`rg-converters ${pkg.version} -> ${version}`);
