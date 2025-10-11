import * as commands_v0_9_0 from "./schemas/v0.9.0/commands.js";
import * as errors_v0_9_0 from "./schemas/v0.9.0/errors.js";
import * as events_v0_9_0 from "./schemas/v0.9.0/events.js";

export const Registry = {
  "0.9.0": {
    commands: commands_v0_9_0,
    events: events_v0_9_0,
    errors: errors_v0_9_0,
  },
} as const;

export type ContractRegistry = typeof Registry;
export type ContractVersion = keyof ContractRegistry;

export function getSchemasFor(version: ContractVersion) {
  return Registry[version];
}
