# AGENTS.md — Coding Assistant Guidelines for Orpheus SDK

**Audience:** AI coding assistants (e.g. ChatGPT, Codex, Copilot, Claude) and human contributors using such assistants.

**Mission:** Maintain the Orpheus SDK as a professional-grade audio engine — a host-neutral C++20 core for deterministic session, transport, and render management — while keeping all "agent" integrations strictly optional.

---

## 🎨 Product Vision & Ecosystem Context

### What We're Building

**Orpheus SDK is the foundation for a sovereign audio tooling ecosystem** — a comprehensive, open-architecture platform for professional audio applications that respects user autonomy, demands reliability, and refuses vendor lock-in.

Think of it as:
- **Audio foundation layer** (like ffmpeg for audio workflows, but with real-time + DAW semantics)
- **Host-neutral core** (portable across DAWs, standalone apps, plugins, embedded systems)
- **Professional-grade reliability** (broadcast-safe, 24/7 operational capability)
- **Open architecture** (MIT licensed core, extensible adapters, no cloud dependencies)

### The Problem We're Solving

Current audio tools are fragmented:
- DAWs use proprietary session formats (Pro Tools, REAPER, Logic, Ableton all incompatible)
- Professional apps are platform-locked (QLab = macOS only, SpotOn = Windows only)
- Cloud-dependent services create single points of failure for production work
- No open, deterministic audio engine suitable for both real-time and offline rendering

Orpheus SDK provides:
- **Deterministic audio graph** (SessionGraph with tracks, clips, tempo, transport)
- **Sample-accurate rendering** (offline bounce or real-time playout)
- **Transport semantics** (punch/loop record, LTC/MTC sync, automation)
- **ABI negotiation** (plugins and hosts safely communicate across versions)
- **Extensible adapter model** (thin integration layers for specific hosts/platforms)

### Applications in the Ecosystem

**First-party applications:**
- **Clip Composer** — Professional soundboard for broadcast, theater, live performance (flagship)
- **Wave Finder** — Harmonic calculator and frequency scope for analysis
- **FX Engine** — LLM-powered effects processing and creative workflows
- **System tools** — Routing matrix, meters, widgets, diagnostics

**Third-party applications** (community-driven):
- DAWs, mixing consoles, spatial renderers
- Installation controllers, broadcast automation
- Educational tools, research platforms

All share the same core philosophy:
- **Local-first** (no mandatory cloud)
- **Deterministic** (same input → same output, always)
- **Professional** (24/7 reliability, sample-accurate)
- **Sovereign** (users own their tools and data)

### SDK Development Philosophy

**Host-Neutral Core:**
- Orpheus SDK doesn't know or care what application uses it
- Same core works in REAPER extensions, standalone apps, embedded systems
- Adapters provide thin integration layers (≤300 LOC when possible)

**Determinism First:**
- Sample-accurate rendering across platforms/architectures
- Reproducible audio output (bounce same session twice → bit-identical files)
- Clock domain isolation (prevent drift, maintain sync)

**Modular Extensibility:**
- Core provides essential graph/transport/render primitives
- Optional adapters add real-time I/O, codecs, DSP, networking
- Applications compose adapters to build complete solutions

**Professional Quality:**
- Broadcast-safe code (no allocations in audio thread)
- Rigorous testing (ctest, AddressSanitizer, cross-platform CI)
- Clear error boundaries (graceful degradation, never crash)

### Strategic Context for AI Assistants

When working on Orpheus SDK code, understand:

1. **This is infrastructure, not an end-user app** — Design APIs for flexibility, not convenience
2. **Multiple applications will use this** — Clip Composer needs real-time triggering, Wave Finder needs FFT analysis, FX Engine needs LLM integration hooks
3. **Determinism is non-negotiable** — If a change breaks sample-accurate rendering or introduces platform-specific behavior, it's wrong
4. **Adapters are optional** — Core must work without any adapters enabled. Applications opt-in via CMake flags
5. **Long-term vision: 10+ years** — Code written today should still work in 2035. Avoid trendy dependencies, favor stable standards

---

## 🎯 Core Technical Objectives

### 1. Primary Target

- **Language:** C++20
- **Scope:** Portable across Windows/macOS/Linux (ARM64 and x86_64)
- **Build System:** CMake + ctest; CI uses clang-format, clang-tidy, AddressSanitizer
- **Core Features:** 
  - SessionGraph (tracks, clips, tempo, transport)
  - Deterministic render path (offline or real-time)
  - ABI negotiation (AbiVersion, safe cross-version communication)
  - Transport management (play, stop, record, loop, sync)
- **Adapters:** Thin layers only (REAPER extension, minhost CLI, WASM/Electron bindings)

### 2. Professional Audio Context

- **Industry standards alignment:**
  - REAPER SDK (ReaScript, extension API)
  - Pro Tools parity goals (deterministic editing, punch/loop record)
  - ADM/OTIO interchange (Audio Definition Model, OpenTimelineIO)
- **Broadcast-safe determinism:**
  - Sample-accurate rendering (no timing drift)
  - Time-base isolation across hosts
  - Reproducible bounces (same session → identical output)
- **Future expansion:**
  - ADM authoring (object-based audio metadata)
  - OSC control (Open Sound Control integration)
  - Clip-grid scheduling (soundboard semantics)
  - Real-time I/O adapters (CoreAudio, ASIO, WASAPI)

### 3. UI Layer (Shmui Integration)

- **Shmui** (Next.js/Turbopack + pnpm workspace) exists to **visualize sessions and prototypes** — not to become the primary UI for applications
- **Components:**
  - Orb (metering/transport visualization)
  - AudioPlayer (local PCM playback in browser)
  - Waveform/timeline prototypes
  - Docs tooling (interactive documentation)
- **No SaaS dependencies:**
  - External APIs remain opt-in, local-mocked, or disabled by default
  - `pnpm --filter www dev` launches mock site on localhost:4000
- **Purpose:** Rapid prototyping and demos, not production UI (apps use JUCE/Electron/native)

---

## 🚫 Out-of-Scope by Default

**Do NOT add these to core or adapters without explicit approval:**

- Embedding third-party SaaS/voice runtimes
- Hard-coded analytics or telemetry
- Non-deterministic "agent orchestration" inside the core engine
- Modifying CMake or CI to require network access

**All "agent" examples must:**
- Compile without network access
- Pass CI in offline mode
- Be disabled by default (opt-in via env vars)
- Live in `packages/shmui` (not core SDK)

---

## 🧭 Repository Signals for AI Assistants

| Layer | Language | Purpose | Notes |
|-------|----------|---------|-------|
| `/src`, `/include` | C++20 | Audio session core | Deterministic, portable, host-neutral |
| `/adapters/reaper_orpheus` | C++ | REAPER extension | Follows REAPER ABI, ≤300 LOC when possible |
| `/adapters/minhost` | C++ | CLI host | Testing reference, minimal I/O |
| `/adapters/realtime_engine/` | C++ | Real-time I/O (future) | CoreAudio, ASIO, WASAPI wrappers |
| `/adapters/clip_grid/` | C++ | Soundboard logic (future) | FIFO choke, group triggering |
| `/packages/shmui` | TypeScript/React | UI demos | Local-mocked, no SaaS dependencies |
| `/packages/engine-wasm`, `/engine-electron` | C++/JS bridge | Optional bindings | Isolated builds |
| `/apps/*` | C++/JUCE | Applications | Clip Composer, Wave Finder, FX Engine, etc. |

**Philosophy:**
- **Core SDK** = `/src`, `/include` (minimal, deterministic)
- **Adapters** = `/adapters/*` (optional, platform-specific)
- **Applications** = `/apps/*` (compose adapters, full features)
- **UI prototypes** = `/packages/shmui` (demos, not production)

---

## 🧩 Safe Implementation Patterns

### C++ Core

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

**Requirements:**
- Enforce `.clang-format` + `.clang-tidy` (CI fails if violated)
- Keep adapters ≤300 LOC when possible
- Preserve float determinism across OSes (use `std::bit_cast`, avoid undefined behavior)
- No allocations in audio callback threads (use lock-free structures, pre-allocate)
- Sample-accurate timing (use 64-bit sample counts, not floating-point seconds)

### UI (Shmui)

- Examples live under `packages/shmui/apps/www/` (Next.js on port 4000)
- Bootstrapped with `pnpm` (`pnpm install` at repo root)
- Keep scripts compatible with `pnpm --filter www …` invocation
- **Always guard networked features behind env vars** (e.g. `VITE_ENABLE_AGENT=0`)
- Use mock data; no hard dependencies on SaaS audio APIs

---

## 🔐 Security Defaults

- **Localhost bind only** (127.0.0.1); never expose 0.0.0.0
- **Tokens only via `.env.local`**; never committed
- **Prefer unsigned local ports** for demo servers
- **No telemetry without explicit consent**
- **Code signing for releases** (applications, not core SDK)

---

## 🎚 "Agent" Semantics in This Repo

**"Agent" = optional orchestration layer external to the Orpheus core**

**Acceptable uses:**
- Mocking transport state (Shmui demos)
- Demonstrating how external services *could* integrate (always optional, mocked)
- LLM integration hooks in FX Engine (application-level, not core)

**Unacceptable uses:**
- Replacing Orpheus render pipeline with SaaS audio streams
- Committing API credentials
- Making core features dependent on external services
- Adding network calls to the render path

**Guideline:** If it's not audio graph/transport/render logic, it doesn't belong in core. Put it in an adapter, application, or Shmui demo.

---

## 🧱 File Placement

| Type | Location | Example |
|------|----------|---------|
| Core / adapters | `src/`, `adapters/` | `SessionGraph.cpp`, `reaper_adapter.cpp` |
| UI demos | `packages/shmui/apps/www/` | `DemoSession.tsx`, `OrbVisualization.tsx` |
| Docs | `docs/` | `ORP068 Implementation Plan.md`, `ARCHITECTURE.md` |
| Applications | `apps/` | `orpheus_clip_composer/`, `orpheus_wave_finder/` |

---

## 🧠 Guidance for Coding Assistants

| Request Type | Expected Output |
|--------------|-----------------|
| Add core feature | C++20 code + CMake + unit tests (ctest), deterministic behavior verified |
| Create UI demo | Next.js/React mock using pnpm workspace, no external API |
| Add background service | Localhost-only, behind flag, documented env vars |
| Update docs | Link back to README + Integration Plan |
| Add adapter | Thin wrapper (≤300 LOC ideal), optional CMake flag, clear separation from core |
| Optimize performance | Profile first, preserve determinism, document trade-offs |

**When in doubt:**
- **Will this work offline?** (If no, it's probably wrong for core SDK)
- **Is this deterministic?** (If no, it doesn't belong in render path)
- **Is this host-neutral?** (If no, it belongs in an adapter)
- **Would this make sense for all applications?** (If no, it's application-specific)

---

## ✅ Do / ❌ Don't Checklist

### Do

- Maintain host-neutral determinism
- Respect sample-accurate rendering and clock domain isolation
- Keep UI examples self-contained and mockable
- Document all flags, ports, and env vars
- Keep CI/build green across OSes
- Write unit tests for new features
- Profile before optimizing
- Preserve backward compatibility (ABI versioning)

### Don't

- Break core/adapter abstraction
- Depend on SaaS runtimes for functionality
- Add unverified external libraries
- Bypass style or test gates (clang-format, clang-tidy, ctest)
- Allocate in audio threads
- Use floating-point time (sample counts only)
- Assume platform specifics (abstract behind adapters)
- Commit secrets or credentials
- Commit binary files

---

## 🚀 Success Criteria for SDK Contributions

**A good SDK contribution:**
1. Compiles on Windows, macOS, Linux without warnings
2. Passes all existing tests (`ctest`)
3. Adds new tests for new functionality
4. Preserves determinism (same input → same output across runs/platforms)
5. Maintains sample-accurate timing
6. Keeps core minimal (no unnecessary dependencies)
7. Documents APIs clearly (Doxygen comments)
8. Follows existing code style (clang-format, clang-tidy compliant)

**A good application contribution:**
1. Uses SDK as a library (doesn't modify core for app-specific features)
2. Composes adapters appropriately
3. Handles errors gracefully (never crashes, logs diagnostics)
4. Provides user-facing documentation
5. Meets performance targets (latency, CPU usage, memory footprint)

---

## 📚 Related Documentation

- **README.md** — Repository overview, getting started, build instructions
- **ARCHITECTURE.md** — Design rationale, component relationships
- **ROADMAP.md** — Planned features, milestones, timeline
- **docs/ADAPTERS.md** — Available adapters, build flags, integration guides
- **OCC021** — Orpheus Clip Composer product vision (flagship application)

---

**Remember:** Orpheus SDK is infrastructure for a sovereign audio ecosystem. We're building tools that professionals can rely on for decades, not chasing trends or optimizing for short-term convenience. When in doubt, favor simplicity, determinism, and user autonomy.
