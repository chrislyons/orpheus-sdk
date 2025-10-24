// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace orpheus {

// Forward declarations
namespace core {
class SessionGraph;
} // namespace core

using ClipHandle = uint64_t;

/// Fade curve types for clip fades
enum class FadeCurve : uint8_t {
  Linear = 0,     ///< f(x) = x
  EqualPower = 1, ///< f(x) = sin(x * π/2) - constant power crossfades
  Exponential = 2 ///< f(x) = x² - dramatic effect
};

/// Error codes for session graph operations
enum class SessionGraphError : uint8_t {
  OK = 0,
  InvalidHandle = 1,
  InvalidParameter = 2,
  NotReady = 3,
  NotSupported = 4,
  NotInitialized = 5,         ///< Added for routing matrix
  InvalidClipTrimPoints = 18, ///< Trim IN >= trim OUT, or out of bounds
  InvalidFadeDuration = 19,   ///< Fade duration > clip duration
  ClipNotRegistered = 20,     ///< Clip handle not found
  InternalError = 255
};

/// Playback state for clips
enum class PlaybackState : uint8_t {
  Stopped = 0, ///< Clip is not playing
  Playing = 1, ///< Clip is actively playing
  Paused = 2,  ///< Clip is paused (reserved for future use)
  Stopping = 3 ///< Clip is fading out before stop
};

/// Sample-accurate transport position
/// Sample counts are authoritative; seconds and beats are derived
struct TransportPosition {
  int64_t samples; ///< Absolute position in samples (authoritative)
  double seconds;  ///< Derived: samples / sample_rate
  double beats;    ///< Derived: seconds * tempo / 60.0
};

/// Callback interface for transport events
/// All callbacks are invoked on the UI thread (NOT audio thread)
class ITransportCallback {
public:
  virtual ~ITransportCallback() = default;

  /// Called when a clip starts playing
  /// @param handle The clip that started
  /// @param position Current transport position
  virtual void onClipStarted(ClipHandle handle, TransportPosition position) = 0;

  /// Called when a clip stops playing
  /// @param handle The clip that stopped
  /// @param position Current transport position
  virtual void onClipStopped(ClipHandle handle, TransportPosition position) = 0;

  /// Called when a clip loops back to start
  /// @param handle The clip that looped
  /// @param position Current transport position
  virtual void onClipLooped(ClipHandle handle, TransportPosition position) = 0;

  /// Called when a buffer underrun occurs (audio dropout)
  /// @param position Position where underrun occurred
  virtual void onBufferUnderrun(TransportPosition position) = 0;
};

/// Transport controller interface for sample-accurate clip playback
///
/// This interface provides real-time control over clip playback with
/// sample-accurate timing and thread-safe operation.
///
/// Thread Safety:
/// - startClip(), stopClip(), stopAllClips(), stopAllInGroup(): Thread-safe, callable from UI
/// thread
/// - getClipState(), isClipPlaying(), getCurrentPosition(): Thread-safe, callable from any thread
/// - setCallback(): UI thread only
///
/// Audio Thread Guarantees:
/// - No allocations in audio callback
/// - Lock-free command processing
/// - Sample-accurate timing (±1 sample tolerance)
class ITransportController {
public:
  virtual ~ITransportController() = default;

  /// Start playback of a specific clip
  ///
  /// This function is thread-safe and can be called from the UI thread.
  /// The clip will begin playing on the next audio buffer callback.
  ///
  /// @param handle The clip to start (must be a valid handle from SessionGraph)
  /// @return SessionGraphError::OK on success, or error code on failure
  ///
  /// @note If the clip is already playing, this function has no effect
  /// @note Playback will honor trim points and fade-in settings from clip metadata
  virtual SessionGraphError startClip(ClipHandle handle) = 0;

  /// Stop playback of a specific clip
  ///
  /// This function is thread-safe and can be called from the UI thread.
  /// The clip will fade out over 10ms (default) before stopping.
  ///
  /// @param handle The clip to stop
  /// @return SessionGraphError::OK on success, or error code on failure
  ///
  /// @note If the clip is not playing, this function has no effect
  /// @note Fade-out duration is configurable via clip metadata
  virtual SessionGraphError stopClip(ClipHandle handle) = 0;

  /// Stop all currently playing clips
  ///
  /// All active clips will fade out simultaneously over 10ms (default).
  ///
  /// @return SessionGraphError::OK on success, or error code on failure
  virtual SessionGraphError stopAllClips() = 0;

  /// Stop all clips in a specific Clip Group
  ///
  /// This is useful for "FIFO choke" behavior where only one clip
  /// in a group can play at a time.
  ///
  /// @param groupIndex Clip Group index (0-3)
  /// @return SessionGraphError::OK on success, or error code on failure
  virtual SessionGraphError stopAllInGroup(uint8_t groupIndex) = 0;

  /// Query the playback state of a specific clip
  ///
  /// This function is thread-safe and can be called from any thread.
  ///
  /// @param handle The clip to query
  /// @return Current playback state
  virtual PlaybackState getClipState(ClipHandle handle) const = 0;

  /// Check if a clip is currently playing
  ///
  /// This is a convenience function equivalent to:
  /// `getClipState(handle) == PlaybackState::Playing`
  ///
  /// @param handle The clip to query
  /// @return true if playing, false otherwise
  virtual bool isClipPlaying(ClipHandle handle) const = 0;

  /// Get the current transport position
  ///
  /// This function is thread-safe and can be called from any thread.
  /// Position is sample-accurate (±1 sample tolerance).
  ///
  /// @return Current transport position (samples, seconds, beats)
  virtual TransportPosition getCurrentPosition() const = 0;

  /// Register a callback for transport events
  ///
  /// Only one callback can be registered at a time. Calling this function
  /// replaces any previously registered callback.
  ///
  /// @param callback Callback interface (pass nullptr to unregister)
  ///
  /// @note This function must be called from the UI thread only
  /// @note Callbacks are invoked on the UI thread, not the audio thread
  virtual void setCallback(ITransportCallback* callback) = 0;

  /// Update trim points for a registered clip
  ///
  /// @param handle Clip handle (must be registered via registerClipAudio)
  /// @param trimInSamples Trim IN point in samples (0 = start of file)
  /// @param trimOutSamples Trim OUT point in samples (file duration = end of file)
  /// @return SessionGraphError::OK on success, error code on failure
  ///
  /// Thread-safe: Can be called from UI thread
  /// Takes effect: On next clip start (does not affect currently playing clips)
  ///
  /// Validation:
  /// - trimInSamples must be >= 0 and < file duration
  /// - trimOutSamples must be > trimInSamples and <= file duration
  /// - If invalid, returns SessionGraphError::InvalidClipTrimPoints
  virtual SessionGraphError updateClipTrimPoints(ClipHandle handle, int64_t trimInSamples,
                                                 int64_t trimOutSamples) = 0;

  /// Update fade settings for a registered clip
  ///
  /// @param handle Clip handle (must be registered via registerClipAudio)
  /// @param fadeInSeconds Fade-in duration in seconds (0.0 = no fade)
  /// @param fadeOutSeconds Fade-out duration in seconds (0.0 = no fade)
  /// @param fadeInCurve Fade-in curve type (Linear, EqualPower, Exponential)
  /// @param fadeOutCurve Fade-out curve type (Linear, EqualPower, Exponential)
  /// @return SessionGraphError::OK on success, error code on failure
  ///
  /// Thread-safe: Can be called from UI thread
  /// Takes effect: On next clip start (does not affect currently playing clips)
  ///
  /// Fade behavior:
  /// - Fade-in: Applied from trimInSamples (0.0 → 1.0 gain over N seconds)
  /// - Fade-out: Applied before trimOutSamples (1.0 → 0.0 gain over N seconds)
  /// - Fade curves:
  ///   - Linear: y = x
  ///   - EqualPower: y = sin(x * π/2)  [smooth crossfades]
  ///   - Exponential: y = x²  [dramatic effect]
  ///
  /// Validation:
  /// - fadeInSeconds must be >= 0.0 and <= (trimOutSamples - trimInSamples) / sampleRate
  /// - fadeOutSeconds must be >= 0.0 and <= (trimOutSamples - trimInSamples) / sampleRate
  /// - If fades overlap, fade-out takes precedence
  virtual SessionGraphError updateClipFades(ClipHandle handle, double fadeInSeconds,
                                            double fadeOutSeconds, FadeCurve fadeInCurve,
                                            FadeCurve fadeOutCurve) = 0;

  /// Get current trim points for a clip (query only)
  ///
  /// @param handle Clip handle
  /// @param[out] trimInSamples Current trim IN point
  /// @param[out] trimOutSamples Current trim OUT point
  /// @return SessionGraphError::OK on success, error code if clip not found
  ///
  /// Thread-safe: Can be called from any thread
  virtual SessionGraphError getClipTrimPoints(ClipHandle handle, int64_t& trimInSamples,
                                              int64_t& trimOutSamples) const = 0;

  /// Update gain for a registered clip
  ///
  /// @param handle Clip handle (must be registered via registerClipAudio)
  /// @param gainDb Gain in decibels (-∞ to +12 dB typical, 0 dB = unity gain)
  /// @return SessionGraphError::OK on success, error code on failure
  ///
  /// Thread-safe: Can be called from UI thread
  /// Takes effect: Immediately for active clips, on next start for stopped clips
  ///
  /// Gain conversion:
  /// - Linear gain = 10^(gainDb / 20)
  /// - Examples: -6 dB = 0.5, 0 dB = 1.0, +6 dB = 2.0
  ///
  /// Validation:
  /// - gainDb must be finite (not NaN or Inf)
  /// - Typical range: -60 dB to +12 dB
  virtual SessionGraphError updateClipGain(ClipHandle handle, float gainDb) = 0;

  /// Set loop mode for a registered clip
  ///
  /// @param handle Clip handle (must be registered via registerClipAudio)
  /// @param shouldLoop true = loop indefinitely, false = play once
  /// @return SessionGraphError::OK on success, error code on failure
  ///
  /// Thread-safe: Can be called from UI thread
  /// Takes effect: On next clip start (does not affect currently playing clips)
  ///
  /// Loop behavior:
  /// - When clip reaches trim OUT point, it seeks back to trim IN point
  /// - Fade-out is NOT applied on loop (fades only apply at manual stop)
  /// - Useful for music beds, ambience, and looping effects
  virtual SessionGraphError setClipLoopMode(ClipHandle handle, bool shouldLoop) = 0;
};

/// Create a transport controller instance
///
/// @param sessionGraph The session graph containing clip metadata
/// @param sampleRate Audio sample rate (e.g., 48000)
/// @return Unique pointer to transport controller
std::unique_ptr<ITransportController> createTransportController(core::SessionGraph* sessionGraph,
                                                                uint32_t sampleRate);

} // namespace orpheus
