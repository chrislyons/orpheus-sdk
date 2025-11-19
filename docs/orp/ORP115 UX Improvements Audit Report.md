# ORP115: UX Improvements Audit Report

**Date:** 2025-11-19
**Scope:** v0.2.1 UX Improvements (commits d44a38c through 076da56)
**Auditor:** Claude Code
**Status:** Complete

---

## Executive Summary

Comprehensive audit of the v0.2.1 UX improvements spanning SDK core and Clip Composer application. The improvements have been merged to main branch (no separate feature branch exists).

**Total Issues Found:** 70+
- **Critical:** 9
- **High:** 17
- **Medium:** 16
- **Low:** 8+

**Key Concerns:**
1. Thread safety violations in audio callback paths
2. Memory management issues (raw pointers, potential leaks)
3. Severely outdated documentation (INDEX files 33+ documents behind)
4. Duplicate documentation directories causing confusion

---

## 1. SDK Core Issues

### Critical Severity

#### 1.1 Mutex Lock in Audio Thread Path
**File:** `src/core/transport/transport_controller.cpp:625`
**Severity:** CRITICAL

```cpp
std::lock_guard<std::mutex> lock(m_audioFilesMutex);
```

`addActiveClip()` acquires mutex while being called from audio callback path via `processCommands() -> processAudio()`.

**Impact:** Violates real-time safety. Can cause audio dropouts if UI thread holds lock.

**Recommendation:** Use lock-free data structure (triple-buffer or RCU pattern).

---

#### 1.2 Memory Leak Risk - Raw Pointer Management
**File:** `src/core/routing/routing_matrix.cpp:69, 729-735, 771`
**Severity:** CRITICAL

Extensive use of raw `new`/`delete` for GainSmoother objects instead of RAII smart pointers.

```cpp
m_master_gain_smoother = new GainSmoother(sample_rate, config.gain_smoothing_ms);
channel.gain_smoother = new GainSmoother(sample_rate, config.gain_smoothing_ms);
```

**Impact:** Memory leak if exceptions occur between allocation and deallocation. Not exception-safe.

**Recommendation:** Replace with `std::unique_ptr<GainSmoother>`.

---

#### 1.3 Loop Fade Logic Edge Case
**File:** `src/core/transport/transport_controller.cpp:265-286, 362-378, 441-484`
**Severity:** CRITICAL

Complex interaction between loop mode changes, fade-in/out, and `hasLoopedOnce` flag. When loop is disabled mid-playback:
- Clip triggers fade-out (line 279-285)
- But fade logic checks `!hasLoopedOnce` (line 362)
- Position clamping happens BEFORE fade logic

**Potential Issue:** Pre-computed fade gain may be stale if position jumps during loop boundary crossing.

**Recommendation:** Add comprehensive unit tests for loop enable/disable transitions.

---

### High Severity

#### 1.4 const_cast Breaking const-correctness
**File:** `src/core/transport/transport_controller.cpp:1035, 1151, 1258, 1306`
**Severity:** HIGH

```cpp
std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_audioFilesMutex));
```

**Recommendation:** Declare `m_audioFilesMutex` as `mutable std::mutex`.

---

#### 1.5 Thread Safety Assumption Not Verified
**File:** `src/core/audio_io/audio_file_reader_libsndfile.cpp:81-85`
**Severity:** HIGH

`readSamples()` called from audio thread with NO mutex protection. Comment states assumption of single-threaded access but no verification.

**Recommendation:** Add runtime assertions or static analysis to verify access pattern.

---

#### 1.6 Mixed Atomic and Non-Atomic Access
**File:** `src/core/transport/transport_controller.cpp:258-264, 391-395`
**Severity:** HIGH

`ActiveClip::currentSample` is `int64_t` (non-atomic) but accessed across threads, while trim points are atomic.

**Impact:** Potential race if UI thread reads position while audio thread updates.

**Recommendation:** Make `currentSample` atomic or document single-producer guarantee.

---

### Medium Severity

#### 1.7 Inefficient Metering Algorithm
**File:** `src/core/routing/routing_matrix.cpp:864-884`
**Severity:** MEDIUM

Two separate loops for peak and RMS calculation - buffer accessed twice.

**Recommendation:** Combine into single loop for cache efficiency.

---

#### 1.8 Floating-Point Time Used
**File:** `src/core/transport/transport_controller.cpp:185, 189`
**Severity:** MEDIUM

Sample counts converted to floating-point seconds/beats. Violates "64-bit sample counts (never float seconds)" principle.

**Impact:** Floating-point rounding errors accumulate over time.

**Recommendation:** Keep samples as authoritative; derive seconds/beats only for display.

---

#### 1.9 Platform-Dependent Constant
**File:** `src/core/transport/transport_controller.cpp:10-12`
**Severity:** MEDIUM

```cpp
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
```

**Recommendation:** Use C++20 `std::numbers::pi`.

---

### Incomplete Implementations (TODOs)

| Location | Description |
|----------|-------------|
| `transport_controller.cpp:62` | SessionGraph integration pending |
| `transport_controller.cpp:187` | Get tempo from SessionGraph |
| `transport_controller.cpp:608` | Optimize to lock-free structure |
| `routing_matrix.cpp:67, 716, 759` | Get sample rate from config |
| `audio_file_reader_libsndfile.cpp:195` | SHA-256 hashing not implemented |
| `scene_manager.cpp:229, 278, 310` | Scene capture/restore incomplete |

---

## 2. Clip Composer Application Issues

### Critical Severity

#### 2.1 Race Condition in Destructor
**File:** `apps/clip-composer/Source/UI/PreviewPlayer.cpp:37-45`
**Severity:** CRITICAL

Timer callback may still be executing when destructor clears callbacks. `stopTimer()` doesn't guarantee callback completion.

**Recommendation:** Stop timer FIRST, then clear callbacks.

---

#### 2.2 TOCTOU in Timer Callback
**File:** `apps/clip-composer/Source/UI/PreviewPlayer.cpp:218-252`
**Severity:** CRITICAL

`isPlaying()` checks if `m_audioEngine` is null, but between check and `getCurrentPosition()`, another thread could destroy it.

**Recommendation:** Cache pointer check or use SafePointer pattern.

---

#### 2.3 Background Thread Memory Safety
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp:49-79`
**Severity:** CRITICAL

`m_waveformCache` map accessed from both background thread (write) and message thread (read). Lock only protects `m_waveformData`, not the cache.

**Recommendation:** Expand lock scope to cover cache access.

---

#### 2.4 Multiple Background Threads for Same File
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp:42-79`
**Severity:** CRITICAL

If `setAudioFile()` called twice quickly, two threads spawn and both generate waveform data.

**Recommendation:** Add thread tracking flag check.

---

#### 2.5 Raw Pointer Dialog Management
**File:** `apps/clip-composer/Source/MainComponent.cpp:872-877, 1001-1003, 1058-1060`
**Severity:** CRITICAL

Manual `new`/`delete` for ClipEditDialog instead of RAII. Memory leak risk if exception thrown.

**Recommendation:** Use `std::unique_ptr` or `Component::SafePointer`.

---

#### 2.6 LookAndFeel Lifecycle Issue
**File:** `apps/clip-composer/Source/MainComponent.cpp:160-169`
**Severity:** CRITICAL

Setting LookAndFeel to `nullptr` doesn't verify child components aren't still using it.

**Recommendation:** Destroy children explicitly before clearing LookAndFeel.

---

### High Severity

#### 2.7 Global Clip Index Not Cached
**File:** `apps/clip-composer/Source/MainComponent.cpp` (40+ locations)
**Severity:** HIGH

`getGlobalClipIndex()` called repeatedly in 75fps polling callbacks. Same calculation done every frame for every button.

**Recommendation:** Cache indices or pass as parameters.

---

#### 2.8 State Synchronization Gap
**File:** `apps/clip-composer/Source/MainComponent.cpp:50-102`
**Severity:** HIGH

75fps callbacks query AudioEngine directly but don't check if clips are still loaded. May show "Playing" for removed clips.

**Recommendation:** Add clip existence check in polling loop.

---

#### 2.9 Playhead Pagination Complexity
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp:90-126`
**Severity:** HIGH

Complex floating-point arithmetic in 75fps callback. Potential for precision errors causing jitter.

**Recommendation:** Simplify logic or add epsilon tolerance.

---

#### 2.10 No Clipboard Visual Feedback
**File:** `apps/clip-composer/Source/MainComponent.cpp:293-359`
**Severity:** HIGH

Cmd+C/V operations provide no visual confirmation. Users can't tell if operation succeeded.

**Recommendation:** Add transient visual feedback (toast notification or button flash).

---

#### 2.11 Blocking Dialog on Paste
**File:** `apps/clip-composer/Source/MainComponent.cpp:316-324`
**Severity:** HIGH

Modal confirmation dialog blocks entire application during paste. Interrupts professional workflow.

**Recommendation:** Add "Don't ask again" checkbox or use non-blocking CallOutBox.

---

#### 2.12 Missing mouseDrag Throttling
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp:324-375`
**Severity:** HIGH

Callback and repaint triggered on every mouse movement (100+ events/sec). No throttling.

**Recommendation:** Throttle to 30-33 FPS during drag operations.

---

#### 2.13 Inefficient Waveform Repaint
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp:186-224`
**Severity:** HIGH

Entire waveform redrawn on every `repaint()` call, even if only playhead moved.

**Recommendation:** Use dirty regions for partial updates.

---

#### 2.14 Inconsistent Error Handling
**File:** `apps/clip-composer/Source/MainComponent.cpp` (multiple)
**Severity:** HIGH

Some AudioEngine calls check return values, others don't (e.g., `startClip()` at line 816).

**Recommendation:** Consistent error handling with user feedback.

---

#### 2.15 Sample Rate Initialization Mismatch
**File:** `apps/clip-composer/Source/MainComponent.cpp:136-151`
**Severity:** HIGH

If `initialize()` fails to set requested sample rate, code continues anyway. May cause distorted audio.

**Recommendation:** Verify actual sample rate after initialization.

---

### Medium Severity

#### 2.16 Duplicated Code in Fade Initialization
**File:** `apps/clip-composer/Source/UI/ClipEditDialog.cpp:136-204`
**Severity:** MEDIUM

68 lines of nearly identical code for fadeIn and fadeOut combo initialization.

**Recommendation:** Extract to helper function.

---

#### 2.17 Complex Nested Conditionals
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp:236-322`
**Severity:** MEDIUM

86-line mouseDown function with 5 levels of nesting. Cyclomatic complexity >10.

**Recommendation:** Refactor into smaller handler functions.

---

#### 2.18 Duplicated Zoom Calculation
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.cpp` (7 locations)
**Severity:** MEDIUM

Same 8-line zoom viewport calculation repeated 7 times.

**Recommendation:** Extract to `calculateViewport()` helper.

---

#### 2.19 Inefficient Key Mapping
**File:** `apps/clip-composer/Source/MainComponent.cpp:414-542`
**Severity:** MEDIUM

Linear search through 48 keys using if-statement chain. O(n) when O(1) possible.

**Recommendation:** Use `std::unordered_map` lookup table.

---

#### 2.20 Magic Numbers Throughout
**Files:** Multiple
**Severity:** MEDIUM

Hardcoded numbers like `1000`, `13`, `30.0f`, `40.0f` without explanation.

**Recommendation:** Define named constants with documentation.

---

#### 2.21 No Accessibility Labels
**Files:** ClipEditDialog.cpp, WaveformDisplay.cpp
**Severity:** MEDIUM

No `setTitle()`, `setDescription()`, or `setHelpText()` calls. Screen readers won't announce purposes.

**Recommendation:** Add accessibility metadata to all interactive components.

---

#### 2.22 Poor Trim Point Error Feedback
**File:** `apps/clip-composer/Source/UI/ClipEditDialog.cpp:819-863`
**Severity:** MEDIUM

When user clicks to set IN point beyond OUT point, code silently clamps. No visual feedback.

**Recommendation:** Add transient visual feedback (flash or tooltip).

---

### Low Severity

#### 2.23 Dead Code
**File:** `apps/clip-composer/Source/UI/WaveformDisplay.h:116`

```cpp
bool m_threeButtonMouseMode = false;  // Never used
```

#### 2.24 Inconsistent Naming Convention
**File:** `apps/clip-composer/Source/MainComponent.cpp`

Mixed callback naming (`getClipState` vs `onStopAll`).

---

## 3. Documentation Issues

### Critical Issues

#### 3.1 Duplicate OCC Documentation Directories
**Location:** `apps/clip-composer/docs/`
**Severity:** CRITICAL

Two directories exist:
- `docs/occ/` (lowercase) - 40 files (OCC090-OCC130)
- `docs/OCC/` (uppercase) - 13 files (OCC129-OCC138)

**Impact:** Confusion about authoritative directory. Case-sensitivity issues across platforms.

**Recommendation:** Consolidate into single lowercase `occ/` directory.

---

#### 3.2 Severely Outdated INDEX Files
**Severity:** CRITICAL

**ORP INDEX (`docs/orp/INDEX.md`):**
- Claims "Next Available Number: ORP111"
- Reality: ORP111-114 already exist

**OCC INDEX (`apps/clip-composer/docs/occ/INDEX.md`):**
- Claims "Next Available Number: OCC105"
- Reality: Documents go up to OCC138 (33 numbers off!)

---

#### 3.3 Broken Links Summary

| File | Broken Links |
|------|--------------|
| `docs/orp/INDEX.md` | ORP068.md, ORP099-101.md, ORP110.md |
| `docs/occ/INDEX.md` | OCC094-095.md, all short-form links |
| `CLAUDE.md` | `docs/ORP/` (wrong case) |
| `docs/GETTING_STARTED.md` | `TROUBLESHOOTING.md` |
| `docs/CONTRIBUTING.md` | `QUICK_START.md`, `docs/integration/` |
| `docs/CONTRACT_DEVELOPMENT.md` | `docs/integration/` |

---

#### 3.4 Archived Documents Referenced as Active

OCC021, OCC026, OCC027 listed as active in INDEX but located in `archive/` subdirectory.

**Referenced by:** CLAUDE.md, OCC INDEX, OCC104, apps/clip-composer/CLAUDE.md

---

#### 3.5 Timeline Inconsistencies

Documents dated January 2025 when current date is November 2025:
- ORP114 - "2025-01-15"
- OCC128 - "January 13, 2025"
- OCC130 variants - "2025-01-14"

CLAUDE.md claims v0.2.0-alpha "pending QA - October 28, 2025" (3 weeks ago).

---

### Medium Issues

#### 3.6 Version Number Confusion

Multiple versions referenced inconsistently:
- v0.1.0-alpha (October 22)
- v0.2.0-alpha (pending QA)
- v0.2.1 (UX fixes)
- v0.2.2 (OCC111, OCC130B)

Unclear which is current/released/in-development.

---

#### 3.7 Archive Count Math Errors

- OCC INDEX: Claims 33 archived (OCC001-039) but that's 39 documents
- ORP INDEX: Claims 18 archived (ORP061-081) but that's 21 documents

---

## 4. Circular Dependencies

### 4.1 SessionGraphError Location
**File:** `include/orpheus/routing_matrix.h:7`

```cpp
#include <orpheus/transport_controller.h> // For SessionGraphError
```

RoutingMatrix includes TransportController header just for error enum.

**Recommendation:** Move SessionGraphError to `errors.h` (partially done - see line 6).

---

## 5. Optimization Opportunities

### 5.1 Performance Optimizations

| Location | Optimization |
|----------|-------------|
| `routing_matrix.cpp:864-884` | Combine peak/RMS loops |
| `WaveformDisplay.cpp` (7 places) | Cache viewport calculation |
| `MainComponent.cpp` (40+ places) | Cache global clip indices |
| `WaveformDisplay.cpp:186-224` | Use dirty region repainting |

### 5.2 Code Quality Improvements

| Issue | Recommendation |
|-------|----------------|
| Raw pointers in RoutingMatrix | Use `std::unique_ptr` |
| const_cast usage | Use `mutable` keyword |
| Duplicated fade init code | Extract helper function |
| Complex mouseDown handler | Split into smaller functions |
| Magic numbers | Define named constants |

---

## 6. Recommendations by Priority

### Immediate (P0) - This Sprint

1. **Fix audio thread mutex lock** - `transport_controller.cpp:625`
2. **Fix PreviewPlayer destructor race** - `PreviewPlayer.cpp:37-45`
3. **Fix WaveformDisplay thread safety** - `WaveformDisplay.cpp:49-79`
4. **Consolidate OCC directories** - Merge `docs/OCC/` into `docs/occ/`
5. **Update INDEX files** - Set correct next available numbers

### High Priority (P1) - Next Sprint

6. Replace raw pointers with smart pointers in RoutingMatrix
7. Fix TOCTOU in PreviewPlayer timer callback
8. Add visual feedback for clipboard operations
9. Fix broken documentation links
10. Add consistent error handling in MainComponent

### Medium Priority (P2) - Backlog

11. Optimize metering algorithm (single loop)
12. Cache global clip indices
13. Add drag throttling in WaveformDisplay
14. Refactor duplicated code (fade init, zoom calc)
15. Add accessibility labels

### Low Priority (P3) - Tech Debt

16. Replace magic numbers with constants
17. Fix naming inconsistencies
18. Remove dead code
19. Update timeline/version references in docs

---

## 7. Testing Recommendations

### Unit Tests Needed

1. Loop enable/disable transitions during playback
2. Fade timing accuracy with concurrent clips
3. Thread safety of currentSample access
4. Trim point boundary enforcement

### Integration Tests Needed

1. 75fps polling with rapid state changes
2. Clipboard operations with multiple tabs
3. Sample rate mismatch handling
4. Background waveform generation cancellation

### Manual QA Checklist

- [ ] Drag playing clips between buttons (check for audio gaps)
- [ ] Zoom/pan during playback (check for visual jitter)
- [ ] Rapid clip start/stop (check for orphaned states)
- [ ] Change loop mode during playback
- [ ] Test on case-sensitive filesystem (Linux)

---

## Appendix: Files Changed in UX Improvements

**Total:** 149 files, +11,040 / -2,400 lines

### Core SDK
- `src/core/transport/transport_controller.cpp` (+57/-46)

### Clip Composer Application
- `Source/MainComponent.cpp` (+489 lines)
- `Source/UI/ClipEditDialog.cpp` (+621 lines)
- `Source/UI/WaveformDisplay.cpp` (+114 lines)
- `Source/ClipGrid/ClipGrid.cpp` (+222 lines)
- `Source/ClipGrid/ClipButton.cpp` (+190 lines)
- `Source/UI/TabSwitcher.cpp` (+222 lines)
- `Source/Session/SessionManager.cpp` (+74 lines)

### Documentation
- Multiple OCC docs moved from `OCC/` to `occ/`
- New OCC127-138 documentation added
- ORP directory case fixed (`ORP/` to `orp/`)

---

**End of Audit Report**

*Generated by Claude Code - ORP115*
