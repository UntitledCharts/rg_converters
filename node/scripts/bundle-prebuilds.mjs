// Copies the C library CI artifacts (<dir>/rg_converters-<platform>/{bin,lib}/...) into prebuilds/.
import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import { basename, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const artifacts = process.argv[2];
if (!artifacts) {
    console.error('usage: node scripts/bundle-prebuilds.mjs <artifacts dir>');
    process.exit(1);
}

const libraries = {
    'win32-x64': 'bin/rg_converters.dll',
    'linux-x64': 'lib/librg_converters.so',
    'linux-arm64': 'lib/librg_converters.so',
    'darwin-universal': 'lib/librg_converters.dylib',
};

const prebuilds = fileURLToPath(new URL('../prebuilds/', import.meta.url));
let missing = false;
for (const [platform, file] of Object.entries(libraries)) {
    const source = join(artifacts, `rg_converters-${platform}`, file);
    if (!existsSync(source)) {
        console.error(`missing ${source}`);
        missing = true;
        continue;
    }
    mkdirSync(join(prebuilds, platform), { recursive: true });
    copyFileSync(source, join(prebuilds, platform, basename(file)));
    console.log(`${platform}: ${source}`);
}
process.exit(missing ? 1 : 0);
