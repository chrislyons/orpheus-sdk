# Contract Version Roadmap

The contract governs interoperability across the Orpheus ecosystem. This roadmap aligns each schema release with migration
phases from the Orpheus Release Plan (ORP063 §II.A) and documents how the schema evolves toward general availability.

## Phase Alignment

| Contract Version     | Lifecycle Window | Migration Focus |
| -------------------- | ---------------- | --------------- |
| **v0.9.0 (alpha)**   | P0 – P1          | Establish baseline handshake, session control, and error taxonomy for proof-of-concept agents. |
| **v1.0.0-beta**      | P2               | Harden negotiation semantics, add adapter coverage, and capture migration guidance for third-party implementers. |
| **v1.0.0**           | P3               | Declare GA for the contract with full compatibility guarantees and long-term support commitments. |
| **v1.1.0 and later** | P4               | Deliver iterative enhancements, additive commands/events, and feature flags governed by semver rules. |

## Version Bump Criteria

Semantic versioning is enforced across schema releases. The `scripts/contract-diff.ts` tool analyzes Zod object signatures and
recommends the required bump:

- **MAJOR**: Removing schemas, removing fields, or changing required fields. These changes break existing integrations and
  require a major version bump.
- **MINOR**: Adding new schemas, adding optional fields, or extending enums/capabilities in backward-compatible ways.
- **PATCH**: Documentation updates, metadata annotations, or other changes that do not affect validation semantics.

Every manifest entry carries a checksum over the source schema files. Changes must be accompanied by an updated checksum. CI
invokes the diff tool to block merges when the proposed version bump does not satisfy the detected change class.

## Deprecation Policy

- Each **stable** contract release (v1.0.0+) is supported for at least two subsequent minor releases. Consumers receive
  deprecation notices through the handshake response payload (`HandshakeAccepted.incompatibleVersions`).
- Deprecated schema elements remain available (but flagged via documentation and response metadata) for a minimum of one full
  migration phase before removal.
- Removal of deprecated schemas or fields must occur only in a major release and must be announced in the preceding phase’s
  roadmap update and release notes.

This policy ensures agents can negotiate forward-compatible behavior while giving implementers clear milestones for adoption.
