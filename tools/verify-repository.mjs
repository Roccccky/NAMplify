#!/usr/bin/env node
// Repository hygiene: required project files, nothing local or secret in the
// source inventory, working documentation links, one license everywhere.
//
//   node tools/verify-repository.mjs [--root <dir>]

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { extname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const rootArgument = process.argv.indexOf('--root');
const root = rootArgument < 0
  ? resolve(fileURLToPath(new URL('..', import.meta.url)))
  : resolve(process.argv[rootArgument + 1] ?? '.');
const failures = [];
const localDirectories = new Set(['.git', 'node_modules', 'build', 'third_party', 'release', 'out']);

function walk (directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    if (entry.isSymbolicLink()) return [];
    const file = join(directory, entry.name);
    if (entry.isDirectory()) return localDirectories.has(entry.name) ? [] : walk(file);
    return [relative(root, file).split(sep).join('/')];
  });
}

let files;
try {
  const gitRoot = execFileSync('git', ['rev-parse', '--show-toplevel'], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
  if (resolve(gitRoot) !== root) throw new Error('not a repository root');
  files = execFileSync('git', ['ls-files', '--cached', '--others', '--exclude-standard', '-z'], { cwd: root, encoding: 'utf8' })
    .split('\0').filter((file) => file && existsSync(join(root, file)));
} catch {
  files = walk(root);
}
files = [...new Set(files)].sort();

for (const required of ['README.md', 'CHANGELOG.md', 'SECURITY.md', 'PRIVACY.md',
                        'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CMakeLists.txt', 'tools/fetch-third-party.sh']) {
  if (!files.includes(required)) failures.push(`Missing required project file: ${required}`);
}

// Build output, fetched sources and installers never belong in the history.
const forbiddenPath = /^(?:build|third_party|release|out|shots|node_modules|docs\/media\/raw)\//i;
const privateFile = /(?:^|\/)(?:\.env(?:\..*)?|[^/]+\.(?:pem|key|p12|pfx|cer|mobileprovision|provisionprofile)|CMakeCache\.txt)$/i;
const secretPatterns = [
  [/\bgh[pousr]_[A-Za-z0-9_]{30,}\b/, 'GitHub token'],
  [/\bgithub_pat_[A-Za-z0-9_]{40,}\b/, 'GitHub fine-grained token'],
  [/\bAKIA[0-9A-Z]{16}\b/, 'AWS access key'],
  [/-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----/, 'Private key'],
  // The TONE3000 key comes from a CI variable, never from the source.
  [/\bt3k_(?:pub|sec)_[A-Za-z0-9]{16,}\b/, 'TONE3000 key'],
];
const textExtensions = new Set(['.md', '.txt', '.json', '.mjs', '.js', '.css', '.html', '.yml', '.yaml',
  '.cmake', '.cpp', '.h', '.hpp', '.mm', '.sh', '.py', '.iss', '.desktop', '.plist', '.in']);
let markdownCount = 0;

for (const file of files) {
  if (forbiddenPath.test(file)) failures.push(`Local or generated material in source inventory: ${file}`);
  if (privateFile.test(file)) failures.push(`Private material in source inventory: ${file}`);
  if (!textExtensions.has(extname(file)) && file !== 'LICENSE' && file !== 'CMakeLists.txt') continue;
  const contents = readFileSync(join(root, file), 'utf8');
  for (const [pattern, label] of secretPatterns)
    if (pattern.test(contents)) failures.push(`${label} pattern in ${file}; inspect locally before sharing`);
  if (extname(file) !== '.md') continue;
  markdownCount++;
  for (const match of contents.matchAll(/!?\[[^\]\n]*\]\((<[^>\n]+>|[^)\n]+)\)|<img [^>]*src="([^"]+)"/g)) {
    const raw = match[1] ?? match[2];
    const href = raw.startsWith('<') ? raw.slice(1, -1) : raw.split(/\s+["']/)[0];
    if (/^(?:[a-z][a-z0-9+.-]*:|#|\/\/)/i.test(href)) continue;
    let target;
    try { target = decodeURIComponent(href.split(/[?#]/)[0]); } catch { failures.push(`Invalid link encoding in ${file}`); continue; }
    if (!target) continue;
    const resolved = target.startsWith('/') ? resolve(root, `.${target}`) : resolve(root, file, '..', target);
    if (!resolved.startsWith(root + sep) && resolved !== root) failures.push(`Documentation link escapes project: ${file} -> ${href}`);
    else if (!existsSync(resolved)) failures.push(`Broken documentation link: ${file} -> ${href}`);
  }
}

// One license, named the same in LICENSE, README and the notices.
try {
  const license = readFileSync(join(root, 'LICENSE'), 'utf8');
  const name = /GNU AFFERO GENERAL PUBLIC LICENSE/i.test(license) ? 'AGPL-3.0'
             : /GNU GENERAL PUBLIC LICENSE/i.test(license) ? 'GPL-3.0'
             : /^MIT License/m.test(license) ? 'MIT' : null;
  if (!name) failures.push('LICENSE is neither AGPL-3.0, GPL-3.0 nor MIT');
  else {
    const readme = readFileSync(join(root, 'README.md'), 'utf8');
    const section = readme.split(/^## License\s*$/m)[1] ?? '';
    if (!section.includes(name)) failures.push(`README "## License" does not name ${name}`);
    const notices = readFileSync(join(root, 'THIRD_PARTY_NOTICES.md'), 'utf8');
    if (!notices.includes(name)) failures.push(`THIRD_PARTY_NOTICES.md does not name NAMplify's own license (${name})`);
  }
} catch (error) {
  failures.push(`Could not validate license metadata: ${error.message}`);
}

if (failures.length) {
  console.error('Repository verification failed:\n' + failures.map((failure) => `- ${failure}`).join('\n'));
  process.exitCode = 1;
} else {
  console.log(`Repository verification passed: ${files.length} source files, ${markdownCount} Markdown documents, license and local links checked.`);
}
