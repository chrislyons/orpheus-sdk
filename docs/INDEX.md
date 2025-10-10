# Orpheus SDK Documentation Index

> Home › Documentation › Index

## 🚀 Start Here

**New to Orpheus SDK?** Begin with these three guides:

1. **[Getting Started](GETTING_STARTED.md)** – Install, build, and run your first session
2. **[Driver Architecture](DRIVER_ARCHITECTURE.md)** – Understand Service, WASM, and Native drivers
3. **[Contract Guide](CONTRACT_DEVELOPMENT.md)** – Learn the command/event schema system

**For specific tasks:**
- Adding features → [Contributor Guide](../CONTRIBUTING.md)
- Migrating projects → [Migration Guide](MIGRATION_GUIDE.md)
- API reference → [API Surface Index](API_SURFACE_INDEX.md)

---

## ORP Migration & Integration Library

| Document | Focus | Key Cross-References |
| --- | --- | --- |
| [ORP061 – Migration Plan](<integration/ORP061 Migration Plan_ Consolidating Shmui UI into Orpheus SDK Monorepo.md>) | Phase-driven roadmap for merging Shmui UI into the Orpheus SDK monorepo. | Establishes Phase 0 baseline that is elaborated in ORP062 (contracts) and optimized in ORP063. |
| [ORP062 – Technical Addendum](<integration/ORP062 Technical Addendum_ Engine Contracts, Drivers, and Integration Guardrails.md>) | Normative contract, driver, and validation rules for the engine boundary. | Extends ORP061 Phase 0/1 requirements; informs latency and frequency limits codified in budgets.json (see PERFORMANCE.md) and references ORP066 refinements. |
| [ORP063 – Technical Optimization](<integration/ORP063 Technical Optimization_ Harmonizing Migration Strategy with Contract Architecture.md>) | Operational alignment between migration strategy and contract architecture. | Builds on ORP061 sequencing; cites ORP062 schemas and feeds Phase 0 CI expectations referenced in ORP066/ORP068. |
| [ORP066 – Implementation Refinements](<integration/ORP066 Technical Addendum_ Implementation Refinements for ORP065.md>) | Corrective refinements and guardrails for ORP065 execution, covering CI, packaging, and risk mitigation. | References ORP061/ORP063 for migration context; drives package naming rules summarized in PACKAGE_NAMING.md. |
| [ORP068 – Integration Plan v2.0](<integration/ORP068 Implementation Plan v2.0_ Orpheus SDK × Shmui Integration .md>) | Updated integration sequencing and validation metrics for later phases. | Supersedes earlier milestones while reaffirming ORP062 contract boundaries and ORP066 refinements. |

### Additional Supporting References

- [ORP065 – Shmui Integration Plan](<integration/ORP065 Implementation Plan v1_1 Orpheus SDK × Shmui Integration.md>) – Historical baseline referenced by ORP066.
- [ORP067 – Task Numbering Appendix](<integration/ORP067 Appendix_ Task Numbering Reference.md>) – Crosswalk between ORP task identifiers and repo workstreams.
- [ORP-CDX-013 – CI Revalidation](<ORP-CDX-013-ci-revalidation.md>) – Companion guidance for validating CI coverage post-migration.

## Migration Phase ↔ Technical Specification Map

| Migration Phase | Core Objectives | Authoritative Specs |
| --- | --- | --- |
| Phase 0 – Preparatory Repository Setup | Establish monorepo structure, parallel CI, package namespace baselines. | ORP061 §Phase 0, ORP062 §§1–3, ORP063 §II, ORP066 §II (package hygiene). |
| Phase 1 – Tooling Normalization | Wire Orpheus engine bindings into Shmui workflows, unify tooling. | ORP061 §Phase 1, ORP062 §§4–5, ORP063 §III, ORP068 §II. |
| Phase 2 – Feature Integration | Deliver UI experiences powered by Orpheus engine capabilities. | ORP061 §Phase 2, ORP063 §IV, ORP068 §III. |
| Phase 3+ – Expansion & Governance | Stabilize releases, enforce performance budgets, broaden platform support. | ORP068 §§IV–V, ORP063 §V, PERFORMANCE.md (budgets.json). |

## Navigation

- ▲ [Back to Repository Overview](../README.md)
- 📦 [Package Naming](PACKAGE_NAMING.md)
- 📈 [Performance Budgets](PERFORMANCE.md)
- 🧪 [CI Validation Checklist](<ORP-CDX-013-ci-revalidation.md>)

### Branch Protection (Manual Verification)
- Ensure **main** has required status checks: `build-cpp`, `build-ui`, `lint-cpp`.
- Require at least 1 reviewer; disallow force-push; require linear history.
- Record verification date and owner here: `docs/GOVERNANCE.md#branch-protection`.
