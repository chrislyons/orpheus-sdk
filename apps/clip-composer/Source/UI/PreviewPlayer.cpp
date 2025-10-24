// SPDX-License-Identifier: MIT

#include "PreviewPlayer.h"

//==============================================================================
PreviewPlayer::PreviewPlayer(AudioEngine* audioEngine) : m_audioEngine(audioEngine) {
  if (!m_audioEngine) {
    DBG("PreviewPlayer: WARNING - AudioEngine is nullptr!");
  } else {
    DBG("PreviewPlayer: Initialized successfully (using Cue Buss architecture)");
  }
}

PreviewPlayer::~PreviewPlayer() {
  // Clear callbacks FIRST to prevent use-after-free
  onPlaybackStopped = nullptr;
  onPositionChanged = nullptr;

  // Release Cue Buss if allocated
  if (m_cueBussHandle != 0 && m_audioEngine) {
    m_audioEngine->stopCueBuss(m_cueBussHandle);
    m_audioEngine->releaseCueBuss(m_cueBussHandle);
  }

  DBG("PreviewPlayer: Destroyed (released Cue Buss " << m_cueBussHandle << ")");
}

//==============================================================================
bool PreviewPlayer::loadFile(const juce::File& audioFile) {
  if (!audioFile.existsAsFile()) {
    DBG("PreviewPlayer: File does not exist: " << audioFile.getFullPathName());
    return false;
  }

  if (!m_audioEngine) {
    DBG("PreviewPlayer: Cannot load file - AudioEngine is nullptr");
    return false;
  }

  stop(); // Stop any current playback

  // Release previous Cue Buss if allocated
  if (m_cueBussHandle != 0) {
    m_audioEngine->releaseCueBuss(m_cueBussHandle);
    m_cueBussHandle = 0;
  }

  // Allocate Cue Buss from AudioEngine
  m_cueBussHandle = m_audioEngine->allocateCueBuss(audioFile.getFullPathName());
  if (m_cueBussHandle == 0) {
    DBG("PreviewPlayer: Failed to allocate Cue Buss for: " << audioFile.getFullPathName());
    return false;
  }

  // Store file path
  m_loadedFilePath = audioFile.getFullPathName();

  // Get metadata from AudioEngine (assuming AudioEngine reads metadata during allocation)
  auto metadata = m_audioEngine->getClipMetadata(0); // TODO: Need API to get Cue Buss metadata
  if (metadata.has_value()) {
    m_sampleRate = static_cast<int>(metadata->sample_rate);
    m_numChannels = static_cast<int>(metadata->num_channels);
    m_totalSamples = static_cast<int64_t>(metadata->duration_samples);
  }

  // Initialize trim points to full file
  m_trimInSamples = 0;
  m_trimOutSamples = m_totalSamples;

  DBG("PreviewPlayer: Loaded " << audioFile.getFileName() << " into Cue Buss " << m_cueBussHandle
                               << " (" << m_sampleRate << " Hz, " << m_numChannels << " ch, "
                               << m_totalSamples << " samples)");

  return true;
}

void PreviewPlayer::setTrimPoints(int64_t trimInSamples, int64_t trimOutSamples) {
  // Only clamp to file boundaries if metadata is loaded (m_totalSamples > 0)
  // DO NOT automatically reset OUT point - preserve user's OUT point always
  if (m_totalSamples > 0) {
    trimInSamples = std::clamp(trimInSamples, int64_t(0), m_totalSamples);
    trimOutSamples = std::clamp(trimOutSamples, int64_t(0), m_totalSamples);

    // No automatic OUT reset! ClipEditDialog validates IN < OUT before calling this.
  }

  m_trimInSamples = trimInSamples;
  m_trimOutSamples = trimOutSamples;

  // Update Cue Buss metadata in AudioEngine
  if (m_cueBussHandle != 0 && m_audioEngine) {
    m_audioEngine->updateCueBussMetadata(m_cueBussHandle, trimInSamples, trimOutSamples,
                                         m_fadeInSeconds, m_fadeOutSeconds, m_fadeInCurve,
                                         m_fadeOutCurve);
  }

  DBG("PreviewPlayer: Trim points set to [" << trimInSamples << ", " << trimOutSamples << "]");
}

void PreviewPlayer::setLoopEnabled(bool shouldLoop) {
  m_loopEnabled = shouldLoop;

  // TODO (SDK Sprint OCC041 Bug #6): Implement loop playback in Orpheus SDK
  // Requires TransportController changes (see OCC041 lines 400-462):
  //
  // 1. Add loop_enabled, loop_in_samples, loop_out_samples to SDK ClipState
  // 2. Implement loop restart logic in TransportController::processAudio()
  // 3. Add AudioEngine::setCueBussLoop() API
  // 4. Wire up PreviewPlayer to call AudioEngine when loop state changes
  //
  // For now, loop state is tracked in metadata and will be synced to clip
  // buttons, but actual audio looping is not yet implemented.

  DBG("PreviewPlayer: Loop " << (shouldLoop ? "enabled" : "disabled")
                             << " (SDK implementation pending - see OCC041)");
}

void PreviewPlayer::setFades(float fadeInSeconds, float fadeOutSeconds,
                             const juce::String& fadeInCurve, const juce::String& fadeOutCurve) {
  m_fadeInSeconds = fadeInSeconds;
  m_fadeOutSeconds = fadeOutSeconds;
  m_fadeInCurve = fadeInCurve;
  m_fadeOutCurve = fadeOutCurve;

  // Update Cue Buss metadata in AudioEngine
  if (m_cueBussHandle != 0 && m_audioEngine) {
    m_audioEngine->updateCueBussMetadata(m_cueBussHandle, m_trimInSamples, m_trimOutSamples,
                                         fadeInSeconds, fadeOutSeconds, fadeInCurve, fadeOutCurve);
  }

  DBG("PreviewPlayer: Fades set to IN=" << fadeInSeconds << "s (" << fadeInCurve << "), OUT="
                                        << fadeOutSeconds << "s (" << fadeOutCurve << ")");
}

void PreviewPlayer::clearFile() {
  stop();

  // Release Cue Buss
  if (m_cueBussHandle != 0 && m_audioEngine) {
    m_audioEngine->releaseCueBuss(m_cueBussHandle);
    m_cueBussHandle = 0;
  }

  m_totalSamples = 0;
  m_loadedFilePath.clear();

  DBG("PreviewPlayer: Cleared file (released Cue Buss)");
}

//==============================================================================
void PreviewPlayer::play() {
  if (m_cueBussHandle == 0 || !m_audioEngine) {
    DBG("PreviewPlayer: Cannot play - no Cue Buss allocated");
    return;
  }

  bool started = m_audioEngine->startCueBuss(m_cueBussHandle);
  if (started) {
    m_isPlaying = true;
    DBG("PreviewPlayer: Started playback (Cue Buss " << m_cueBussHandle << ")");
  } else {
    DBG("PreviewPlayer: Failed to start Cue Buss " << m_cueBussHandle);
  }
}

void PreviewPlayer::stop() {
  if (m_cueBussHandle != 0 && m_audioEngine) {
    m_audioEngine->stopCueBuss(m_cueBussHandle);
  }

  m_isPlaying = false;

  // Notify UI
  if (onPlaybackStopped) {
    onPlaybackStopped();
  }

  DBG("PreviewPlayer: Stopped playback (Cue Buss " << m_cueBussHandle << ")");
}

void PreviewPlayer::jumpTo(int64_t samplePosition) {
  // Clamp to trim range
  samplePosition = std::clamp(samplePosition, m_trimInSamples, m_trimOutSamples);

  // TODO: Implement transport jump in AudioEngine Cue Buss (future enhancement)
  DBG("PreviewPlayer: Jump to sample " << samplePosition << " (not yet implemented in Cue Buss)");
}

int64_t PreviewPlayer::getCurrentPosition() const {
  // TODO: Get real position from AudioEngine
  // For now, return 0 (position tracking will be added in future iteration)
  return 0;
}

//==============================================================================
void PreviewPlayer::startPositionTimer() {
  // TODO: Implement position polling timer (if needed)
}

void PreviewPlayer::stopPositionTimer() {
  // TODO: Implement position polling timer (if needed)
}
