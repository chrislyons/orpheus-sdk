#!/usr/bin/env tsx
import { createHash } from "crypto";
import { existsSync, readFileSync, readdirSync, statSync } from "fs";
import path from "path";
import process from "process";
import ts from "typescript";

interface ManifestEntry {
  version: string;
  path: string;
  checksum: string;
  status: string;
}

interface Manifest {
  currentVersion: string;
  currentPath: string;
  checksum: string;
  availableVersions: ManifestEntry[];
}

interface SchemaInfo {
  name: string;
  kind: "commands" | "events" | "errors" | "other";
  file: string;
  hash: string;
  properties: string[];
}

interface SchemaChange {
  schema: SchemaInfo;
  target?: SchemaInfo;
  change: "added" | "removed" | "property-added" | "property-removed" | "modified";
  severity: "MAJOR" | "MINOR" | "PATCH";
  details?: Record<string, unknown>;
}

interface DiffReport {
  baseVersion: string;
  targetVersion: string;
  classification: "NONE" | "PATCH" | "MINOR" | "MAJOR";
  checksumValidated: boolean;
  changes: SchemaChange[];
}

const cwd = process.cwd();
const PACKAGE_ROOT = existsSync(path.join(cwd, "MANIFEST.json"))
  ? cwd
  : path.resolve(cwd, "packages/contract");
const MANIFEST_PATH = path.join(PACKAGE_ROOT, "MANIFEST.json");

function loadManifest(): Manifest {
  try {
    const raw = readFileSync(MANIFEST_PATH, "utf8");
    const manifest = JSON.parse(raw) as Manifest;
    if (!Array.isArray(manifest.availableVersions)) {
      manifest.availableVersions = [];
    }
    return manifest;
  } catch (error) {
    console.error("Unable to read MANIFEST.json. Have you generated it?");
    process.exit(1);
  }
}

function ensureVersion(manifest: Manifest, version: string): ManifestEntry {
  const entry = manifest.availableVersions.find((candidate) => candidate.version === version);
  if (!entry) {
    console.error(`Version ${version} not found in manifest.`);
    process.exit(1);
  }
  return entry;
}

function gatherFiles(rootDir: string): string[] {
  const files: string[] = [];
  const stack = ["."];
  while (stack.length > 0) {
    const rel = stack.pop()!;
    const abs = path.join(rootDir, rel);
    const stats = statSync(abs);
    if (stats.isDirectory()) {
      for (const entry of readdirSync(abs)) {
        stack.push(path.join(rel, entry));
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

function computeChecksum(versionPath: string) {
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

function verifyChecksum(entry: ManifestEntry): boolean {
  const { digest } = computeChecksum(entry.path);
  if (digest !== entry.checksum) {
    console.error(
      `Checksum mismatch for ${entry.version}. Manifest has ${entry.checksum} but computed ${digest}.`
    );
    process.exit(1);
  }
  return true;
}

const printer = ts.createPrinter({ removeComments: true });

function normalizeExpression(node: ts.Expression | undefined, source: ts.SourceFile): string {
  if (!node) {
    return "";
  }
  const printed = printer.printNode(ts.EmitHint.Expression, node, source);
  return printed.replace(/\s+/g, " ").trim();
}

function collectProperties(node: ts.Expression | undefined): string[] {
  if (!node) {
    return [];
  }
  if (ts.isCallExpression(node)) {
    const callee = node.expression;
    if (
      (ts.isPropertyAccessExpression(callee) && callee.name.text === "object") ||
      (ts.isIdentifier(callee) && callee.text === "object")
    ) {
      const [firstArg] = node.arguments;
      if (firstArg && ts.isObjectLiteralExpression(firstArg)) {
        const properties = new Set<string>();
        for (const prop of firstArg.properties) {
          if (ts.isPropertyAssignment(prop) || ts.isShorthandPropertyAssignment(prop)) {
            if (ts.isIdentifier(prop.name)) {
              properties.add(prop.name.text);
            } else if (ts.isStringLiteral(prop.name)) {
              properties.add(prop.name.text);
            }
          } else if (ts.isSpreadAssignment(prop)) {
            properties.add(`...${prop.expression.getText()}`);
          }
        }
        return Array.from(properties).sort();
      }
    }
    if (ts.isPropertyAccessExpression(callee) || ts.isIdentifier(callee)) {
      return collectProperties(node.expression as ts.Expression);
    }
  }
  if (ts.isCallExpression(node) && node.arguments.length > 0) {
    return collectProperties(node.arguments[0]);
  }
  return [];
}

function analyzeFile(filePath: string, kind: SchemaInfo["kind"]): SchemaInfo[] {
  const content = readFileSync(filePath, "utf8");
  const source = ts.createSourceFile(filePath, content, ts.ScriptTarget.Latest, true, ts.ScriptKind.TS);
  const schemas: SchemaInfo[] = [];

  function handleVariableStatement(statement: ts.VariableStatement) {
    const isExported = statement.modifiers?.some((modifier) => modifier.kind === ts.SyntaxKind.ExportKeyword);
    if (!isExported) {
      return;
    }
    for (const declaration of statement.declarationList.declarations) {
      if (!ts.isIdentifier(declaration.name)) {
        continue;
      }
      const name = declaration.name.text;
      const initializer = declaration.initializer && ts.isAsExpression(declaration.initializer)
        ? declaration.initializer.expression
        : (declaration.initializer as ts.Expression | undefined);
      const normalized = normalizeExpression(initializer, source);
      const hash = createHash("sha256").update(normalized).digest("hex");
      const properties = collectProperties(initializer);
      schemas.push({
        name,
        kind,
        file: filePath,
        hash,
        properties,
      });
    }
  }

  for (const statement of source.statements) {
    if (ts.isVariableStatement(statement)) {
      handleVariableStatement(statement);
    }
  }

  return schemas;
}

function analyzeVersion(entry: ManifestEntry): SchemaInfo[] {
  const absolute = path.join(PACKAGE_ROOT, entry.path);
  const files = gatherFiles(absolute).map((rel) => path.join(absolute, rel));
  const schemas: SchemaInfo[] = [];
  for (const file of files) {
    let kind: SchemaInfo["kind"] = "other";
    if (file.includes("commands")) {
      kind = "commands";
    } else if (file.includes("events")) {
      kind = "events";
    } else if (file.includes("errors")) {
      kind = "errors";
    }
    schemas.push(...analyzeFile(file, kind));
  }
  return schemas;
}

function classifyChanges(changes: SchemaChange[]): DiffReport["classification"] {
  const severities = new Set<SchemaChange["severity"]>();
  for (const change of changes) {
    severities.add(change.severity);
  }
  if (severities.has("MAJOR")) {
    return "MAJOR";
  }
  if (severities.has("MINOR")) {
    return "MINOR";
  }
  if (severities.has("PATCH")) {
    return "PATCH";
  }
  return "NONE";
}

function diffVersions(base: ManifestEntry, target: ManifestEntry): DiffReport {
  verifyChecksum(base);
  verifyChecksum(target);

  const baseSchemas = analyzeVersion(base);
  const targetSchemas = analyzeVersion(target);

  const changes: SchemaChange[] = [];
  const baseKey = (schema: SchemaInfo) => `${schema.kind}:${schema.name}`;
  const targetMap = new Map(targetSchemas.map((schema) => [baseKey(schema), schema]));
  const baseMap = new Map(baseSchemas.map((schema) => [baseKey(schema), schema]));

  for (const schema of baseSchemas) {
    const key = baseKey(schema);
    if (!targetMap.has(key)) {
      changes.push({ schema, change: "removed", severity: "MAJOR" });
      continue;
    }
    const targetSchema = targetMap.get(key)!;
    const removedProperties = schema.properties.filter((prop) => !targetSchema.properties.includes(prop));
    const addedProperties = targetSchema.properties.filter((prop) => !schema.properties.includes(prop));
    if (removedProperties.length > 0) {
      changes.push({
        schema,
        target: targetSchema,
        change: "property-removed",
        severity: "MAJOR",
        details: { removedProperties },
      });
      continue;
    }
    if (addedProperties.length > 0) {
      changes.push({
        schema,
        target: targetSchema,
        change: "property-added",
        severity: "MINOR",
        details: { addedProperties },
      });
      continue;
    }
    if (schema.hash !== targetSchema.hash) {
      changes.push({
        schema,
        target: targetSchema,
        change: "modified",
        severity: "PATCH",
      });
    }
  }

  for (const schema of targetSchemas) {
    const key = baseKey(schema);
    if (!baseMap.has(key)) {
      changes.push({ schema, change: "added", severity: "MINOR" });
    }
  }

  const classification = classifyChanges(changes);
  return {
    baseVersion: base.version,
    targetVersion: target.version,
    classification,
    checksumValidated: true,
    changes,
  };
}

function main() {
  const [baseVersion, targetVersion] = process.argv.slice(2);
  if (!baseVersion || !targetVersion) {
    console.error("Usage: pnpm contract:diff <base> <target>");
    process.exit(1);
  }

  const manifest = loadManifest();
  const base = ensureVersion(manifest, baseVersion);
  const target = ensureVersion(manifest, targetVersion);
  const report = diffVersions(base, target);
  console.log(JSON.stringify(report, null, 2));
}

main();
