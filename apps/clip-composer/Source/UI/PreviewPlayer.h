// SPDX-License-Identifier: MIT

#pragma once

#include "../Audio/AudioEngine.h"
#include <atomic>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

// Forward declaration
class AudioEngine;

/**
 * PreviewPlayer - Preview audio player for Edit Dialog using Cue Buss
 *
 * ARCHITECTURE CHANGE (v0.2.0):
 * - No longer creates separate audio device (was causing overlapping streams)
 * - Now uses Cue Buss allocated from AudioEngine
 * - Cue Buss sums to main output (professional workflow)
 * - Single CoreAudio device for entire application
 *
 * Features:
 * - Play/Stop with respect to IN/OUT trim points
 * - Loop mode (restart at IN when reaching OUT)
 * - Position tracking with callback for waveform playhead
 * - Sample-accurate trim point enforcement
 * - Routes through Cue Buss (not separate audio stream)
 */
class PreviewPlayer {
public:
  PreviewPlayer(AudioEngine* audioEngine);
  ~PreviewPlayer();

  //==============================================================================
  // Setup

  /// Load audio file for preview
  /// @param audioFile Path to audio file
  /// @return true on success
  bool loadFile(const juce::File& audioFile);

  /// Set trim points (IN/OUT) in samples
  /// @param trimInSamples Trim IN point (sample position)
  /// @param trimOutSamples Trim OUT point (sample position)
  void setTrimPoints(int64_t trimInSamples, int64_t trimOutSamples);

  /// Set loop mode
  /// @param shouldLoop True to loop between IN/OUT points
  void setLoopEnabled(bool shouldLoop);

  /// Set fade times and curves
  /// @param fadeInSeconds Fade-in duration in seconds
  /// @param fadeOutSeconds Fade-out duration in seconds
  /// @param fadeInCurve Fade-in curve type ("Linear", "EqualPower", "Exponential")
  /// @param fadeOutCurve Fade-out curve type ("Linear", "EqualPower", "Exponential")
  void setFades(float fadeInSeconds, float fadeOutSeconds, const juce::String& fadeInCurve,
                const juce::String& fadeOutCurve);

  /// Clear loaded file and stop playback
  void clearFile();

  //==============================================================================
  // Playback Control

  /// Start playback from trim IN point
  void play();

  /// Stop playback
  void stop();

  /// Jump to specific sample position
  /// @param samplePosition Sample position to jump to (clamped to IN/OUT range)
  void jumpTo(int64_t samplePosition);

  /// Check if currently playing
  bool isPlaying() const {
    return m_isPlaying;
  }

  /// Get current playback position in samples
  int64_t getCurrentPosition() const;

  //==============================================================================
  // Callbacks

  /// Called when playback position changes (for waveform playhead visualization)
  std::function<void(int64_t samplePosition)> onPositionChanged;

  /// Called when playback stops (reached end or manual stop)
  std::function<void()> onPlaybackStopped;

private:
  //==============================================================================
  // Cue Buss architecture
  AudioEngine* m_audioEngine = nullptr;    // Non-owning reference
  orpheus::ClipHandle m_cueBussHandle = 0; // 0 = not allocated
  juce::String m_loadedFilePath;           // Track loaded file path

  // Playback state
  bool m_isPlaying = false;
  bool m_loopEnabled = false;

  // Trim points (for UI state tracking)
  int64_t m_trimInSamples = 0;
  int64_t m_trimOutSamples = 0;

  // Fade settings (for UI state tracking)
  float m_fadeInSeconds = 0.0f;
  float m_fadeOutSeconds = 0.0f;
  juce::String m_fadeInCurve = "Linear";
  juce::String m_fadeOutCurve = "Linear";

  // File metadata
  int m_sampleRate = 48000;
  int m_numChannels = 2;
  int64_t m_totalSamples = 0;

  // Position tracking timer
  void startPositionTimer();
  void stopPositionTimer();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviewPlayer)
};
