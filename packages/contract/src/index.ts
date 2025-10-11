import { z } from "zod";

export * from "./registry.js";
export * as v0_9_0 from "./schemas/v0.9.0/index.js";

const ContractManifestEntrySchema = z.object({
  version: z.string(),
  path: z.string(),
  checksum: z.string(),
  status: z.enum(["alpha", "beta", "stable", "deprecated"]),
});

const ContractManifestSchema = z.object({
  currentVersion: z.string(),
  currentPath: z.string(),
  checksum: z.string(),
  availableVersions: z.array(ContractManifestEntrySchema),
});

import manifestJson from "../MANIFEST.json" with { type: "json" };

export const MANIFEST = ContractManifestSchema.parse(manifestJson);

export type ContractManifest = z.infer<typeof ContractManifestSchema>;
export type ContractManifestEntry = z.infer<typeof ContractManifestEntrySchema>;
