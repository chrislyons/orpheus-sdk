import { z } from "zod";

/**
 * Semantic version string matcher used for contract negotiation.
 */
export const Semver = z
  .string()
  .regex(/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-.]+)?$/, "Invalid semver string");

export const SessionLocator = z.discriminatedUnion("type", [
  z.object({
    type: z.literal("file"),
    path: z.string().min(1, "File path is required"),
  }),
  z.object({
    type: z.literal("inline"),
    encoding: z.enum(["json", "base64"]).default("json"),
    payload: z.string().min(1, "Inline payload cannot be empty"),
  }),
]);

export const CapabilityDescriptor = z.object({
  id: z.string().min(1, "Capability id is required"),
  version: Semver.optional(),
  description: z.string().optional(),
});

export const HandshakeRequest = z.object({
  kind: z.literal("orpheus.contract/handshake.request"),
  agent: z.object({
    name: z.string().min(1, "Agent name is required"),
    version: z.string().min(1, "Agent version is required"),
    capabilities: z.array(CapabilityDescriptor).default([]),
  }),
  supportedVersions: z.array(Semver).nonempty(),
  preferredVersion: Semver.optional(),
  session: z
    .object({
      intent: z.enum(["load", "inspect", "monitor"]).default("load"),
    })
    .optional(),
});

export const LoadSession = z.object({
  kind: z.literal("orpheus.contract/load-session"),
  sessionId: z.string().min(1, "Session id is required"),
  locator: SessionLocator,
  allowCreate: z.boolean().default(false),
  expectedContractVersion: Semver.optional(),
});

export type CapabilityDescriptor = z.infer<typeof CapabilityDescriptor>;
export type HandshakeRequest = z.infer<typeof HandshakeRequest>;
export type LoadSession = z.infer<typeof LoadSession>;
export type SessionLocator = z.infer<typeof SessionLocator>;
export type Semver = z.infer<typeof Semver>;
