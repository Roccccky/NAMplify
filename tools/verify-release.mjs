#!/usr/bin/env node
// Release checks: one version everywhere, license notices in every installer,
// the demo in its shipping state.
//
//   node tools/verify-release.mjs                  source checks
//   node tools/verify-release.mjs --tag v2.0.0-beta.1
//   node tools/verify-release.mjs --artifacts out  plus the built installers

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const arg = (name) => { const i = process.argv.indexOf(name); return i < 0 ? null : process.argv[i + 1]; };
const read = (file) => readFileSync(join(root, file), 'utf8');
const failures = [];

// Version. The number lives in project(), a pre-release suffix next to it.
// Together they are the label people see, the tag and the changelog heading.
const cmake = read('CMakeLists.txt');
const version = cmake.match(/project\s*\(\s*NAMplify\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)/)?.[1];
if (!version) failures.push('No version in CMakeLists.txt (project(NAMplify VERSION x.y.z))');
const suffix = cmake.match(/set\s*\(\s*NAMPLIFY_VERSION_SUFFIX\s+"([^"]*)"\s*\)/)?.[1];
if (suffix === undefined) failures.push('No NAMPLIFY_VERSION_SUFFIX in CMakeLists.txt');
else if (suffix && !/^-[0-9A-Za-z.-]+$/.test(suffix)) failures.push(`NAMPLIFY_VERSION_SUFFIX "${suffix}" is not a pre-release suffix`);
const label = `${version}${suffix ?? ''}`;

const changelog = existsSync(join(root, 'CHANGELOG.md')) ? read('CHANGELOG.md') : '';
const latest = changelog.match(/^##\s+([0-9][^\s]*)/m)?.[1];
if (!latest) failures.push('CHANGELOG.md has no version heading (## x.y.z…)');
else if (version && latest !== label) failures.push(`CHANGELOG.md starts with ${latest}, CMakeLists.txt says ${label}`);

const mockVersion = read('ui/mock.js').match(/^\s*version:\s*'([^' ]+)/m)?.[1];
if (mockVersion !== label) failures.push(`ui/mock.js shows version ${mockVersion}, CMakeLists.txt says ${label}`);

const tag = arg('--tag');
if (tag) {
  const m = tag.match(/^v([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.-]+)?$/);
  if (!m) failures.push(`Tag ${tag} is not v<major>.<minor>.<patch>[-pre]`);
  else {
    if (tag !== `v${label}`) failures.push(`Tag ${tag} does not match CMakeLists.txt version ${label}`);
    if (!new RegExp(`^##\\s+${tag.slice(1).replaceAll('.', '\\.')}\\b`, 'm').test(changelog))
      failures.push(`CHANGELOG.md has no section for ${tag.slice(1)}`);
  }
}

// License in every installer
for (const file of ['LICENSE', 'THIRD_PARTY_NOTICES.md'])
  if (!existsSync(join(root, file))) failures.push(`${file} is missing`);

const iss = read('installer/NAMplify.iss');
if (!/^LicenseFile=\.\.\\LICENSE\s*$/m.test(iss)) failures.push('Windows installer shows no license (LicenseFile)');
if (!/Source:\s*"\.\.\\LICENSE"/.test(iss) || !/Source:\s*"\.\.\\THIRD_PARTY_NOTICES\.md"/.test(iss))
  failures.push('Windows installer does not install LICENSE and THIRD_PARTY_NOTICES.md');

const mac = read('tools/make-release.sh');
if (!/cp LICENSE "\$b\/Contents\/Resources\/LICENSE\.txt"/.test(mac) || !/THIRD_PARTY_NOTICES\.md "\$b\/Contents\/Resources\//.test(mac))
  failures.push('macOS package does not carry LICENSE and THIRD_PARTY_NOTICES.md');

const workflow = read('.github/workflows/release.yml');
if (!/cp LICENSE THIRD_PARTY_NOTICES\.md "out\/\$name\/"/.test(workflow))
  failures.push('Linux archive does not carry LICENSE and THIRD_PARTY_NOTICES.md');

// Demo shipping state. The demo asks about the input on start, like the app on
// a new computer. Only the setting itself counts, not a comment that mentions
// it.
const onStart = read('ui/mock.js').match(/^\s*onStart:\s*(\d+)/m)?.[1];
if (onStart !== '0') failures.push(`ui/mock.js: onStart is ${onStart ?? 'missing'}, must be 0 in a release`);

// Built installers
const artifacts = arg('--artifacts');
let checked = 0;
if (artifacts) {
  const dir = resolve(artifacts);
  const files = existsSync(dir) ? readdirSync(dir).filter((f) => f.startsWith('NAMplify_')) : [];
  if (!files.length) failures.push(`No NAMplify_* installers in ${artifacts}`);
  for (const file of files) {
    const path = join(dir, file);
    checked++;
    if (!/^NAMplify_[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?_(macos_universal\.pkg|windows_x64_setup\.exe|linux_x64\.tar\.gz)$/.test(file))
      failures.push(`Unexpected installer name: ${file}`);
    if (version && !file.startsWith(`NAMplify_${label}_`)) failures.push(`${file} does not carry version ${label}`);
    if (statSync(path).size < 1_000_000) failures.push(`${file} is suspiciously small`);
    if (file.endsWith('.tar.gz')) {
      const list = execFileSync('tar', ['-tzf', path], { encoding: 'utf8' });
      for (const need of ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'install.sh', 'NAMplify.vst3/'])
        if (!list.split('\n').some((l) => l.endsWith('/' + need) || l.endsWith('/' + need.replace(/\/$/, '')) || l.includes('/' + need)))
          failures.push(`${file} lacks ${need}`);
    }
    if (file.endsWith('.pkg') && process.platform === 'darwin') {
      const payload = execFileSync('pkgutil', ['--payload-files', path], { encoding: 'utf8' });
      if (!payload.includes('Contents/Resources/LICENSE.txt')) failures.push(`${file} lacks LICENSE.txt`);
      if (!payload.includes('Contents/Resources/THIRD_PARTY_NOTICES.md')) failures.push(`${file} lacks THIRD_PARTY_NOTICES.md`);
    }
  }
}

if (failures.length) {
  console.error('Release verification failed:\n' + failures.map((f) => `- ${f}`).join('\n'));
  process.exit(1);
}
console.log(`Release verification passed for NAMplify ${label}${tag ? ` (${tag})` : ''}${checked ? `: ${checked} installers checked` : ''}.`);
