import { createHash } from "crypto";
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from "fs";
import path from "path";
import process from "process";

const cwd = process.cwd();
const PACKAGE_ROOT = existsSync(path.join(cwd, "MANIFEST.json"))
  ? cwd
  : path.resolve(cwd, "packages/contract");
const MANIFEST_PATH = path.join(PACKAGE_ROOT, "MANIFEST.json");
const CURRENT_VERSION = "0.9.0";
const CURRENT_PATH = "src/schemas/v0.9.0";

const args = new Set(process.argv.slice(2));
const shouldWrite = args.has("--write");

function gatherFiles(rootDir) {
  const files = [];
  const stack = ["."];
  while (stack.length > 0) {
    const rel = stack.pop();
    const abs = path.join(rootDir, rel);
    const stats = statSync(abs);
    if (stats.isDirectory()) {
      for (const entry of readdirSync(abs)) {
        const nextRel = path.join(rel, entry);
        stack.push(nextRel);
      }
      continue;
    }
    if (!/\.(?:ts|js)$/i.test(rel)) {
      continue;
    }
    files.push(rel.replace(/\\/g, "/"));
  }
  return files.sort((a, b) => a.localeCompare(b));
}

function computeChecksum(versionPath) {
  const absolute = path.join(PACKAGE_ROOT, versionPath);
  const files = gatherFiles(absolute);
  const hash = createHash("sha256");
  for (const file of files) {
    const content = readFileSync(path.join(absolute, file));
    hash.update(file);
    hash.update("\0");
    hash.update(content);
  }
  return { digest: `sha256:${hash.digest("hex")}`, files };
}

function readManifest() {
  try {
    const raw = readFileSync(MANIFEST_PATH, "utf8");
    return JSON.parse(raw);
  } catch (error) {
    return {
      currentVersion: CURRENT_VERSION,
      currentPath: CURRENT_PATH,
      checksum: "",
      availableVersions: [],
    };
  }
}

const manifest = readManifest();
manifest.availableVersions = Array.isArray(manifest.availableVersions)
  ? manifest.availableVersions
  : [];
const { digest, files } = computeChecksum(CURRENT_PATH);
const versionEntryIndex = manifest.availableVersions.findIndex(
  (entry) => entry.version === CURRENT_VERSION
);
const entry = {
  version: CURRENT_VERSION,
  path: CURRENT_PATH,
  checksum: digest,
  status: "alpha",
};

if (versionEntryIndex >= 0) {
  manifest.availableVersions[versionEntryIndex] = {
    ...manifest.availableVersions[versionEntryIndex],
    ...entry,
  };
} else {
  manifest.availableVersions.push(entry);
}

manifest.currentVersion = CURRENT_VERSION;
manifest.currentPath = CURRENT_PATH;
manifest.checksum = digest;

if (shouldWrite) {
  writeFileSync(MANIFEST_PATH, `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Wrote manifest with checksum: ${digest}`);
  console.log(`Files included in checksum (${files.length}):`);
  for (const file of files) {
    console.log(` - ${path.join(CURRENT_PATH, file)}`);
  }
  process.exit(0);
}

try {
  const existing = JSON.parse(readFileSync(MANIFEST_PATH, "utf8"));
  const recordedEntry = existing.availableVersions.find(
    (item) => item.version === CURRENT_VERSION
  );
  if (!recordedEntry) {
    console.error(
      `Version ${CURRENT_VERSION} missing from manifest. Run with --write to update.`
    );
    process.exit(1);
  }
  if (recordedEntry.checksum !== digest || existing.checksum !== digest) {
    console.error(
      `Checksum mismatch for version ${CURRENT_VERSION}. Expected ${recordedEntry.checksum}, computed ${digest}.`
    );
    process.exit(1);
  }
  console.log(`Checksum verified for version ${CURRENT_VERSION}: ${digest}`);
} catch (error) {
  console.error(`Unable to read manifest. Run with --write to generate it.`);
  process.exit(1);
}
