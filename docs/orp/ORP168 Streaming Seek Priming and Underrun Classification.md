<!-- SPDX-License-Identifier: MIT -->

# ORP168 — Streaming Seek Priming and Underrun Classification

**Document type:** Transport contract correction and downstream handoff
**Version target:** Unreleased; SDK version remains `0.6.7`
**Status:** Implemented and verified on targeted Debug and ThreadSanitizer gates
**Date:** 2026-07-31
**Request:** OCC176 [1]

---

## Scope

ORP168 corrects the existing `ITransportController::seekClip(ClipHandle, int64_t)` contract for registered streaming sources without changing its signature, vtable slot, C ABI, or FIFO/all-active-voice semantics. An accepted seek now has a reader-validated, pinned first-render working set before its command is published. Clip Composer adoption and SDK pinning remain deferred until a tagged release.

## Implementation

`StreamingClipSource` retains its four-page steady worker window and adds four fixed command-prime pages (`kNumPages == 8`). `PrimeReservation` is an eight-bit POD lease. It supports transactional control-thread priming across a bounded multi-page range, including extensions for loop starts and segment-program starts.

Every page now has an atomic guard:

- `0` is unpinned;
- positive values are command pins;
- `UINT32_MAX` is the transient worker-fill/audio-retirement claim.

The worker claims only steady-window FREE pages, decodes with `decodePage()` while the page remains unpublished, then release-publishes `start`. Command priming claims only command-prime capacity, decodes every missing page under the reader mutex, and publishes none until the complete request succeeds. Failure releases only pins claimed by that request and leaves the caller token unchanged. Audio retirement cannot reclaim a positive-pin page; command release is fixed bounded atomic work after the render block.

`prefill()` now returns `SessionGraphError`: the audible page is mandatory and look-ahead remains best effort. Initial streaming construction and registered start/refire preparation propagate that result rather than storing a source with an unprepared trim-IN page. Unprimed cache misses still return `false` from `read()` without blocking.

`ResamplingAudioFileReader` now propagates wrapped-reader failures from `produceUntil()` instead of treating them as EOF and flushing a synthetic silence tail. A failed wrapped seek leaves the prior resampler state and target position intact.

`TransportController` now retains a control-allocated `SourceCommandLifetime` for each unread registered-source Start or Seek command. Seek preparation checks ring capacity before reader work; primes `4 * maxBlockFrames + 2` source frames; extends its one reservation for a looping trim-IN or every segment-program start; retains the lifetime; and publishes one existing Seek command. Page reservations remain through every source read in the consuming callback and then release. Unread lifetimes and pending page primes block unregistration/replacement. Destruction stops the stream worker, releases unread command leases and retained render-block reservations, then destroys the registry.

## Public contract

`seekClip` still clamps to file bounds and applies one command at the next valid render boundary to every active voice. It now returns:

- `InternalError` before preparation when the bounded command ring is full;
- `NotReady` when no voice is published or fixed command-prime capacity is unavailable;
- a concrete reader/preparation error otherwise.

Every rejection is failure-atomic: it publishes no command, changes no cursor, emits no `ClipSeeked`, and synthesizes no `BufferUnderrun`. `seekToCuePoint` continues to delegate to this corrected path.

Existing registrations are not replaced in place. Registered-source start preparation failures propagate instead of using a source-less fallback. Active voices, unread registered-source Starts/Seeks, and pending streaming primes block unregister until clean ownership cutover.

## Rendered regression evidence

`streaming_seek_test` contains 14 contracts, including:

1. a 128-case matrix over 44.1/48/96/192 kHz engine rates, 64/127/512/1024-frame blocks, mono/quad layouts, 0.5x/2x rates, and native/mismatched file rates;
2. 16-bit position-coded WAV markers proving first post-seek audio is the CED-like target marker on every output channel with no underrun;
3. 4x page-boundary coverage, queued FIFO seeks, command-ring saturation, and first-block RT allocation/deallocation guards;
4. transactional read/seek/early-EOF failures, fixed-prime-capacity recovery, and concurrent worker/prime ownership coverage;
5. resampled-reader error propagation and failed-seek state preservation;
6. trim-loop coverage with an evicted trim-IN page for seeks at and past trim-OUT, segment transition, fade-overlap, queued Start, exact-EOF Seek, and unregister/replacement lifetime coverage.

Observed on this branch:

- focused build of all requested transport targets: passed;
- `streaming_seek_test`: 14/14 passed;
- `realtime_harness_test --gtest_filter='RealtimeHarnessTest.Streaming*'`: 5/5 passed;
- requested focused CTest gate and `tools/realtime_audit.py --fail-known-debt`: passed; the audit reported zero hard failures and zero tracked debt findings;
- ThreadSanitizer build plus `voice_state_tsan_test` (2/2), streaming realtime harness (5/5), and `streaming_seek_test` (14/14): passed with `halt_on_error=1`.

The configured tree built successfully. Its full CTest run passed 157/159 tests. Two local gates failed: `docs_path_audit` reported eight stale repository links in `README.md`/`ARCHITECTURE.md`; `coreaudio_driver_test` reported unavailable/default CoreAudio route conditions and a sample-rate-monitor timing expectation on this host. Neither failure exercised ORP168 code.

## Downstream boundary

This SDK-only change does not modify `/Users/chrislyons/dev/clip-composer`, create an OCC-specific API, retry hidden seeks, add a second player, suppress genuine underruns, or bump the SDK project version. Clip Composer should pin a tagged SDK release before adding its OCC176 CED OUT → IN → grid integration regression.

## References

[1] C. Lyons, “OCC176 — Streaming Seek Priming and Underrun Classification SDK Request,” Clip Composer engineering request, Jul. 30, 2026. Local source: `~/dev/clip-composer/docs/occ/OCC176 Streaming Seek Priming and Underrun Classification SDK Request.md`. [Accessed: Jul. 31, 2026].
