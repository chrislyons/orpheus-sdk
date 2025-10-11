import { z } from "zod";
import { Semver } from "./commands.js";

export const EngineErrorCode = z.enum([
  "ENGINE_STARTUP_FAILED",
  "SESSION_NOT_FOUND",
  "SESSION_LOAD_FAILED",
  "SESSION_VALIDATION_FAILED",
  "CONTRACT_MISMATCH",
  "UNSUPPORTED_VERSION",
  "COMMAND_REJECTED",
  "INTERNAL_ERROR",
]);

export const EngineErrorContext = z.object({
  sessionId: z.string().optional(),
  command: z.string().optional(),
  contractVersion: Semver.optional(),
});

export const EngineError = z.object({
  kind: z.literal("orpheus.contract/error"),
  code: EngineErrorCode,
  message: z.string().min(1, "Error message is required"),
  retryable: z.boolean().default(false),
  fatal: z.boolean().default(false),
  context: EngineErrorContext.optional(),
  details: z.record(z.string(), z.unknown()).optional(),
});

export type EngineError = z.infer<typeof EngineError>;
export type EngineErrorCode = z.infer<typeof EngineErrorCode>;
export type EngineErrorContext = z.infer<typeof EngineErrorContext>;
