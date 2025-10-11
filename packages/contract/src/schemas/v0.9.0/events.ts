import { z } from "zod";
import { CapabilityDescriptor, Semver } from "./commands.js";

const SessionSummary = z.object({
  trackCount: z.number().int().nonnegative(),
  clipCount: z.number().int().nonnegative().optional(),
  lastModifiedAt: z.string().datetime().optional(),
});

export const HandshakeAccepted = z.object({
  kind: z.literal("orpheus.contract/handshake.accepted"),
  negotiatedVersion: Semver,
  server: z.object({
    name: z.string().min(1, "Server name is required"),
    version: z.string().min(1, "Server version is required"),
    capabilities: z.array(CapabilityDescriptor).default([]),
  }),
  heartbeatIntervalMs: z.number().int().positive().optional(),
  incompatibleVersions: z
    .array(
      z.object({
        version: Semver,
        reason: z.string().optional(),
      })
    )
    .default([]),
});

export const HandshakeRejected = z.object({
  kind: z.literal("orpheus.contract/handshake.rejected"),
  supportedVersions: z.array(Semver).nonempty(),
  reason: z.string().min(1),
});

export const SessionChanged = z.object({
  kind: z.literal("orpheus.contract/session-changed"),
  sessionId: z.string().min(1, "Session id is required"),
  revision: z.number().int().nonnegative(),
  changeId: z.string().uuid(),
  summary: SessionSummary,
});

export type HandshakeAccepted = z.infer<typeof HandshakeAccepted>;
export type HandshakeRejected = z.infer<typeof HandshakeRejected>;
export type SessionChanged = z.infer<typeof SessionChanged>;
export type SessionSummary = z.infer<typeof SessionSummary>;
