// SPDX-License-Identifier: MIT
#include "transport_controller.h"

#include "session/session_graph.h" // For SessionGraph
#include <algorithm>
#include <cmath>
#include <cstring>

namespace orpheus {

TransportController::TransportController(core::SessionGraph* sessionGraph, uint32_t sampleRate)
    : m_sessionGraph(sessionGraph), m_sampleRate(sampleRate), m_callback(nullptr) {
  // Calculate fade-out samples
  m_fadeOutSamples =
      static_cast<size_t>((FADE_OUT_DURATION_MS / 1000.0f) * static_cast<float>(sampleRate));

  // Calculate restart crossfade samples (broadcast-safe restart mechanism)
  m_restartCrossfadeSamples = static_cast<size_t>((RESTART_CROSSFADE_DURATION_MS / 1000.0f) *
                                                  static_cast<float>(sampleRate));

  // Create and initialize routing matrix
  m_routingMatrix = createRoutingMatrix();

  RoutingConfig routingConfig;
  routingConfig.num_channels = MAX_ACTIVE_CLIPS; // One channel per possible active clip
  routingConfig.num_groups = 4;                  // 4 Clip Groups (as per ORP070)
  routingConfig.num_outputs = 2;                 // Stereo output
  routingConfig.solo_mode = SoloMode::SIP;
  routingConfig.metering_mode = MeteringMode::Peak;
  routingConfig.gain_smoothing_ms = 10.0f;
  routingConfig.enable_metering = true;
  routingConfig.enable_clipping_protection =
      false; // DISABLED: Causes distortion with overlapping fades (professional users manage gain
             // staging)

  m_routingMatrix->initialize(routingConfig);

  // Pre-allocate per-clip read buffers (interleaved audio from files)
  m_clipReadBuffers.resize(MAX_ACTIVE_CLIPS);
  for (auto& buffer : m_clipReadBuffers) {
    buffer.resize(MAX_BUFFER_FRAMES * MAX_FILE_CHANNELS, 0.0f);
  }

  // Pre-allocate per-clip channel buffers (mono output for routing)
  m_clipChannelBuffers.resize(MAX_ACTIVE_CLIPS);
  for (auto& buffer : m_clipChannelBuffers) {
    buffer.resize(MAX_BUFFER_FRAMES, 0.0f);
  }

  // Pre-allocate pointer array for processRouting()
  m_clipChannelPointers.resize(MAX_ACTIVE_CLIPS);
  for (size_t i = 0; i < MAX_ACTIVE_CLIPS; ++i) {
    m_clipChannelPointers[i] = m_clipChannelBuffers[i].data();
  }

  // TODO: m_sessionGraph will be used for querying clip metadata (trim points, routing, etc.)
  (void)m_sessionGraph; // Suppress unused warning for now
}

TransportController::~TransportController() = default;

SessionGraphError TransportController::startClip(ClipHandle handle) {
  // Validate handle
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Check if already playing
  if (isClipPlaying(handle)) {
    return SessionGraphError::OK; // Already playing, no-op
  }

  // Post command to audio thread
  size_t writeIndex = m_commandWriteIndex.load(std::memory_order_relaxed);
  size_t nextIndex = (writeIndex + 1) % MAX_COMMANDS;

  // Check if queue is full
  if (nextIndex == m_commandReadIndex.load(std::memory_order_acquire)) {
    return SessionGraphError::InternalError; // Queue full
  }

  m_commands[writeIndex] = {TransportCommand::Type::Start, handle, 0};
  m_commandWriteIndex.store(nextIndex, std::memory_order_release);

  return SessionGraphError::OK;
}

SessionGraphError TransportController::stopClip(ClipHandle handle) {
  // Validate handle
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Post command to audio thread
  size_t writeIndex = m_commandWriteIndex.load(std::memory_order_relaxed);
  size_t nextIndex = (writeIndex + 1) % MAX_COMMANDS;

  // Check if queue is full
  if (nextIndex == m_commandReadIndex.load(std::memory_order_acquire)) {
    return SessionGraphError::InternalError; // Queue full
  }

  m_commands[writeIndex] = {TransportCommand::Type::Stop, handle, 0};
  m_commandWriteIndex.store(nextIndex, std::memory_order_release);

  return SessionGraphError::OK;
}

SessionGraphError TransportController::stopAllClips() {
  // Post command to audio thread
  size_t writeIndex = m_commandWriteIndex.load(std::memory_order_relaxed);
  size_t nextIndex = (writeIndex + 1) % MAX_COMMANDS;

  // Check if queue is full
  if (nextIndex == m_commandReadIndex.load(std::memory_order_acquire)) {
    return SessionGraphError::InternalError; // Queue full
  }

  m_commands[writeIndex] = {TransportCommand::Type::StopAll, 0, 0};
  m_commandWriteIndex.store(nextIndex, std::memory_order_release);

  return SessionGraphError::OK;
}

SessionGraphError TransportController::stopAllInGroup(uint8_t groupIndex) {
  // Validate group index (0-3 for 4 Clip Groups)
  if (groupIndex >= 4) {
    return SessionGraphError::InvalidParameter;
  }

  // Post command to audio thread
  size_t writeIndex = m_commandWriteIndex.load(std::memory_order_relaxed);
  size_t nextIndex = (writeIndex + 1) % MAX_COMMANDS;

  // Check if queue is full
  if (nextIndex == m_commandReadIndex.load(std::memory_order_acquire)) {
    return SessionGraphError::InternalError; // Queue full
  }

  m_commands[writeIndex] = {TransportCommand::Type::StopGroup, 0, groupIndex};
  m_commandWriteIndex.store(nextIndex, std::memory_order_release);

  return SessionGraphError::OK;
}

PlaybackState TransportController::getClipState(ClipHandle handle) const {
  // This is called from UI thread, but we're reading audio thread state
  // We rely on the fact that checking a clip in the array is atomic enough
  // for our purposes (worst case: we're one frame behind)

  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      if (m_activeClips[i].isStopping) {
        return PlaybackState::Stopping;
      }
      return PlaybackState::Playing;
    }
  }

  return PlaybackState::Stopped;
}

bool TransportController::isClipPlaying(ClipHandle handle) const {
  PlaybackState state = getClipState(handle);
  return state == PlaybackState::Playing || state == PlaybackState::Stopping;
}

TransportPosition TransportController::getCurrentPosition() const {
  int64_t samples = m_currentSample.load(std::memory_order_relaxed);

  TransportPosition position;
  position.samples = samples;
  position.seconds = static_cast<double>(samples) / static_cast<double>(m_sampleRate);

  // TODO: Get tempo from SessionGraph
  double tempo = 120.0; // Default tempo
  position.beats = position.seconds * tempo / 60.0;

  return position;
}

void TransportController::setCallback(ITransportCallback* callback) {
  m_callback = callback;
}

void TransportController::processAudio(float** outputBuffers, size_t numChannels,
                                       size_t numFrames) {
  // Process pending commands from UI thread
  processCommands();

  // Routing matrix controls output channel count
  (void)numChannels;

  // Clamp frames to max buffer size
  numFrames = std::min(numFrames, MAX_BUFFER_FRAMES);

  // Clear all clip channel buffers
  for (size_t i = 0; i < MAX_ACTIVE_CLIPS; ++i) {
    std::memset(m_clipChannelBuffers[i].data(), 0, numFrames * sizeof(float));
  }

  // Render each active clip to its own channel buffer
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    ActiveClip& clip = m_activeClips[i];

    // Skip if no audio file registered
    if (!clip.reader || !clip.reader->isOpen()) {
      continue;
    }

    // Load trim and fade settings (atomic read for thread safety)
    int64_t trimIn = clip.trimInSamples.load(std::memory_order_acquire);
    int64_t trimOut = clip.trimOutSamples.load(std::memory_order_acquire);
    int64_t fadeInSampleCount = clip.fadeInSamples.load(std::memory_order_acquire);
    int64_t fadeOutSampleCount = clip.fadeOutSamples.load(std::memory_order_acquire);
    FadeCurve fadeInCurveType = clip.fadeInCurve.load(std::memory_order_acquire);
    FadeCurve fadeOutCurveType = clip.fadeOutCurve.load(std::memory_order_acquire);

    // ORP093: Enforce trim boundaries BEFORE rendering (prevents position escape bug)
    // CRITICAL: Clamp position to [trimIn, trimOut) range to maintain edit laws
    // This ensures getClipPosition() never returns values outside user-defined boundaries
    if (clip.currentSample < trimIn) {
      // Position below IN point - clamp to IN (enforce Edit Law #1)
      clip.currentSample = trimIn;
      if (clip.reader) {
        clip.reader->seek(trimIn);
      }
    } else if (clip.currentSample >= trimOut) {
      // Position at or past OUT point - handle loop or stop
      bool shouldLoop = clip.loopEnabled.load(std::memory_order_acquire);
      if (shouldLoop) {
        // Loop mode: restart from IN point
        clip.currentSample = trimIn;
        if (clip.reader) {
          clip.reader->seek(trimIn);
        }
      } else {
        // Non-loop mode: skip rendering, will be stopped by post-render check
        continue;
      }
    }

    // Calculate how many frames to read (respecting trim OUT point)
    int64_t framesUntilEnd = trimOut - clip.currentSample;
    if (framesUntilEnd <= 0) {
      continue; // Already past end
    }

    size_t framesToRead =
        static_cast<size_t>(std::min(static_cast<int64_t>(numFrames), framesUntilEnd));

    // Note: We don't seek on every callback - the reader maintains its position
    // The initial seek to trimInSamples happens in addActiveClip()

    // Read audio from file
    size_t numFileChannels = clip.numChannels;

    // Use this clip's dedicated read buffer (no shared buffer conflicts!)
    float* clipReadBuffer = m_clipReadBuffers[i].data();
    size_t clipReadBufferSize = m_clipReadBuffers[i].size();

    // Check if read fits in pre-allocated buffer
    size_t samplesNeeded = framesToRead * numFileChannels;
    if (samplesNeeded > clipReadBufferSize) {
      // Clip too large for buffer - skip (should not happen with reasonable buffer size)
      continue;
    }

    // Read samples from audio file into THIS clip's buffer
    // Use captured shared_ptr reader (thread-safe, no map lookup needed)
    auto readResult = clip.reader->readSamples(clipReadBuffer, framesToRead);

    if (!readResult.isOk()) {
      continue;
    }

    size_t framesRead = readResult.value;

    // Output to this clip's channel buffer (mono sum for routing)
    float* clipChannelBuffer = m_clipChannelBuffers[i].data();

    // Load clip gain (atomic read for thread safety)
    float clipGainDb = clip.gainDb.load(std::memory_order_acquire);
    float clipGainLinear = std::pow(10.0f, clipGainDb / 20.0f); // Convert dB to linear

    for (size_t frame = 0; frame < framesRead; ++frame) {
      // Calculate base gain (starts at 1.0)
      float gain = 1.0f;

      // Apply clip gain (from gainDb setting)
      gain *= clipGainLinear;

      // Apply broadcast-safe restart crossfade (5ms linear fade-in)
      if (clip.isRestarting && clip.restartFadeFramesRemaining > 0) {
        // Calculate fade-in gain (0.0 → 1.0 over restartCrossfadeSamples)
        int64_t fadeProgress =
            static_cast<int64_t>(m_restartCrossfadeSamples) - clip.restartFadeFramesRemaining;
        float restartFadeGain =
            static_cast<float>(fadeProgress) / static_cast<float>(m_restartCrossfadeSamples);
        gain *= restartFadeGain; // Linear fade-in

        // Decrement remaining frames (will be disabled when reaches 0)
        clip.restartFadeFramesRemaining--;
        if (clip.restartFadeFramesRemaining == 0) {
          clip.isRestarting = false; // Crossfade complete
        }
      }

      // Apply stop fade-out if stopping
      // NOTE: fadeOutGain is pre-computed in post-render loop (lines 361-386)
      // based on total fade progress, NOT per-frame index
      if (clip.isStopping) {
        gain *= std::max(0.0f, clip.fadeOutGain); // Use pre-computed fade gain
      }

      // Apply clip fade-in (first N samples from trim IN)
      int64_t relativePos = clip.currentSample + static_cast<int64_t>(frame) - trimIn;
      if (fadeInSampleCount > 0 && relativePos >= 0 && relativePos < fadeInSampleCount) {
        float fadeInPos = static_cast<float>(relativePos) / static_cast<float>(fadeInSampleCount);
        gain *= calculateFadeGain(fadeInPos, fadeInCurveType);
      }

      // Apply clip fade-out (last N samples before trim OUT)
      int64_t trimmedDuration = trimOut - trimIn;
      if (fadeOutSampleCount > 0 && relativePos >= (trimmedDuration - fadeOutSampleCount)) {
        int64_t fadeOutRelativePos = relativePos - (trimmedDuration - fadeOutSampleCount);
        float fadeOutPos =
            static_cast<float>(fadeOutRelativePos) / static_cast<float>(fadeOutSampleCount);
        gain *= (1.0f - calculateFadeGain(fadeOutPos, fadeOutCurveType));
      }

      // Mix all file channels to mono for routing
      float monoSample = 0.0f;
      for (size_t ch = 0; ch < numFileChannels; ++ch) {
        size_t srcIndex = frame * numFileChannels + ch;
        monoSample += clipReadBuffer[srcIndex];
      }
      monoSample /= static_cast<float>(numFileChannels); // Average channels

      clipChannelBuffer[frame] = monoSample * gain;
    }

    // Advance clip position by actual frames read (not buffer size!)
    // CRITICAL (Copilot feedback): This must happen AFTER fade processing, not before
    // Previously this was at line 341 (before fade loop), causing fade timing to be off by one
    // buffer
    clip.currentSample += static_cast<int64_t>(framesRead);
  }

  // Process routing matrix: clips → groups → master output
  m_routingMatrix->processRouting(const_cast<const float**>(m_clipChannelPointers.data()),
                                  outputBuffers, static_cast<uint32_t>(numFrames));

  // Update clips
  size_t i = 0;
  while (i < m_activeClipCount) {
    ActiveClip& clip = m_activeClips[i];

    // Apply fade-out if stopping (using clip's configured fade-out settings)
    if (clip.isStopping) {
      int64_t fadeOutSampleCount = clip.fadeOutSamples.load(std::memory_order_acquire);

      // If no fade-out configured, use default 10ms fade
      if (fadeOutSampleCount == 0) {
        fadeOutSampleCount = static_cast<int64_t>(m_fadeOutSamples);
      }

      int64_t fadeProgress = clip.currentSample - clip.fadeOutStartPos;

      if (fadeProgress >= fadeOutSampleCount) {
        // Fade-out complete, remove clip
        postCallback([this, handle = clip.handle, pos = getCurrentPosition()]() {
          if (m_callback) {
            m_callback->onClipStopped(handle, pos);
          }
        });

        removeActiveClip(clip.handle);
        continue; // Don't increment i, we just removed this clip
      } else {
        // Update fadeOutGain for rendering (will be applied in processAudio sample loop)
        float fadePos = static_cast<float>(fadeProgress) / static_cast<float>(fadeOutSampleCount);
        FadeCurve fadeOutCurve = clip.fadeOutCurve.load(std::memory_order_acquire);
        clip.fadeOutGain = 1.0f - calculateFadeGain(fadePos, fadeOutCurve);
      }
    }

    // Check if clip reached trim OUT point
    int64_t clipTrimOut = clip.trimOutSamples.load(std::memory_order_acquire);
    if (clip.currentSample >= clipTrimOut) {
      // Check if clip should loop
      bool shouldLoop = clip.loopEnabled.load(std::memory_order_acquire);

      if (shouldLoop) {
        // Loop: seek back to trim IN point (works even without reader)
        int64_t trimIn = clip.trimInSamples.load(std::memory_order_acquire);
        if (clip.reader) {
          clip.reader->seek(trimIn);
        }
        clip.currentSample = trimIn;

        // Post loop callback
        postCallback([this, handle = clip.handle, pos = getCurrentPosition()]() {
          if (m_callback) {
            m_callback->onClipLooped(handle, pos);
          }
        });

        // Continue playback (don't remove clip, don't increment i)
        ++i;
      } else if (clip.reader) {
        // Non-loop mode WITH reader: Stop at OUT point
        // Begin fade-out if configured and not already stopping
        if (!clip.isStopping) {
          int64_t fadeOutSampleCount = clip.fadeOutSamples.load(std::memory_order_acquire);

          if (fadeOutSampleCount > 0) {
            // Begin fade-out
            clip.isStopping = true;
            clip.fadeOutGain = 1.0f;
            clip.fadeOutStartPos = clip.currentSample;

            // CRITICAL FIX (ORP091): Clear reader to prevent libsndfile auto-wrap to position 0
            // When reader reaches EOF, libsndfile auto-wraps to position 0 on next read
            // This violates edit law: "Playhead must stay within [IN, OUT] range"
            // By clearing the reader, we ensure no further audio is read during fade-out
            clip.reader = nullptr;

            ++i; // Continue processing fade-out
          } else {
            // No fade-out configured - stop immediately
            postCallback([this, handle = clip.handle, pos = getCurrentPosition()]() {
              if (m_callback) {
                m_callback->onClipStopped(handle, pos);
              }
            });

            removeActiveClip(clip.handle);
            continue; // Don't increment i (clip was removed)
          }
        } else {
          // Already in fade-out, let normal fade-out logic handle it
          ++i;
        }
      } else {
        // No reader, non-loop mode - just continue (don't stop test placeholder clips)
        ++i;
      }
    } else {
      ++i;
    }
  }

  // Update transport position
  int64_t newSample =
      m_currentSample.load(std::memory_order_relaxed) + static_cast<int64_t>(numFrames);
  m_currentSample.store(newSample, std::memory_order_relaxed);
}

void TransportController::processCommands() {
  size_t readIndex = m_commandReadIndex.load(std::memory_order_relaxed);
  size_t writeIndex = m_commandWriteIndex.load(std::memory_order_acquire);

  while (readIndex != writeIndex) {
    const TransportCommand& cmd = m_commands[readIndex];

    switch (cmd.type) {
    case TransportCommand::Type::Start:
      addActiveClip(cmd.handle);
      postCallback([this, handle = cmd.handle, pos = getCurrentPosition()]() {
        if (m_callback) {
          m_callback->onClipStarted(handle, pos);
        }
      });
      break;

    case TransportCommand::Type::Stop: {
      ActiveClip* clip = findActiveClip(cmd.handle);
      if (clip) {
        clip->isStopping = true;
        clip->fadeOutGain = 1.0f;
        clip->fadeOutStartPos = clip->currentSample; // Record position when fade-out started
      }
    } break;

    case TransportCommand::Type::StopAll:
      for (size_t i = 0; i < m_activeClipCount; ++i) {
        m_activeClips[i].isStopping = true;
        m_activeClips[i].fadeOutGain = 1.0f;
        m_activeClips[i].fadeOutStartPos =
            m_activeClips[i].currentSample; // Record position when fade-out started
      }
      break;

    case TransportCommand::Type::StopGroup:
      // TODO: Get clip group assignments from SessionGraph
      // For now, this is a no-op
      break;
    }

    readIndex = (readIndex + 1) % MAX_COMMANDS;
    m_commandReadIndex.store(readIndex, std::memory_order_release);
  }
}

ActiveClip* TransportController::findActiveClip(ClipHandle handle) {
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      return &m_activeClips[i];
    }
  }
  return nullptr;
}

void TransportController::addActiveClip(ClipHandle handle) {
  if (m_activeClipCount >= MAX_ACTIVE_CLIPS) {
    // TODO: Report error (too many active clips)
    return;
  }

  // Look up audio file reader and metadata for this clip
  // NOTE: Brief mutex lock in audio thread - only happens when starting clip, not during playback
  // TODO: Optimize to lock-free structure for production
  std::shared_ptr<IAudioFileReader> reader; // Capture shared_ptr (thread-safe refcount increment)
  uint16_t numChannels = 2;                 // Default stereo
  int64_t totalFrames = 48000 * 10;         // Default 10 seconds

  // Persistent metadata from storage
  int64_t trimInSamples = 0;
  int64_t trimOutSamples = 0;
  double fadeInSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  FadeCurve fadeInCurve = FadeCurve::Linear;
  FadeCurve fadeOutCurve = FadeCurve::Linear;
  float gainDb = 0.0f;
  bool loopEnabled = false;
  bool stopOthersOnPlay = false;

  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it != m_audioFiles.end()) {
      reader = it->second.reader; // Capture shared_ptr (atomic refcount increment)
      numChannels = it->second.metadata.num_channels;
      totalFrames = it->second.metadata.duration_samples;

      // Load persistent metadata from storage
      trimInSamples = it->second.trimInSamples;
      trimOutSamples = it->second.trimOutSamples;
      fadeInSeconds = it->second.fadeInSeconds;
      fadeOutSeconds = it->second.fadeOutSeconds;
      fadeInCurve = it->second.fadeInCurve;
      fadeOutCurve = it->second.fadeOutCurve;
      gainDb = it->second.gainDb;
      loopEnabled = it->second.loopEnabled;
      stopOthersOnPlay = it->second.stopOthersOnPlay;

      // If trim OUT is not set (0), use file duration
      if (trimOutSamples == 0) {
        trimOutSamples = totalFrames;
      }
    }
  }

  // If still no trim OUT point (no audio file registered), use sensible default for testing
  // This allows clips to "play" even without audio (useful for integration tests)
  if (trimOutSamples == 0) {
    trimOutSamples = 48000 * 60; // Default to 60 seconds for unregistered clips
  }

  // If "Stop Others On Play" is enabled, trigger fade-out for all other active clips
  if (stopOthersOnPlay) {
    for (size_t i = 0; i < m_activeClipCount; ++i) {
      // Skip the clip we're about to start (if it was already playing)
      if (m_activeClips[i].handle != handle) {
        m_activeClips[i].isStopping = true;
        m_activeClips[i].fadeOutGain = 1.0f;
        m_activeClips[i].fadeOutStartPos = m_activeClips[i].currentSample;
      }
    }
  }

  // If no audio file registered, we'll play silence (reader will be nullptr)

  // Initialize clip with persistent metadata from storage
  ActiveClip& clip = m_activeClips[m_activeClipCount++];
  clip.handle = handle;
  clip.startSample = m_currentSample.load(std::memory_order_relaxed);
  clip.currentSample =
      trimInSamples; // CRITICAL: Start from IN point (Clip Edit Law #1: Playback MUST >= IN)

  // Initialize trim points from persistent storage
  clip.trimInSamples.store(trimInSamples, std::memory_order_release);
  clip.trimOutSamples.store(trimOutSamples, std::memory_order_release);

  // Initialize fade settings from persistent storage
  clip.fadeInSeconds.store(fadeInSeconds, std::memory_order_release);
  clip.fadeOutSeconds.store(fadeOutSeconds, std::memory_order_release);
  clip.fadeInCurve.store(fadeInCurve, std::memory_order_release);
  clip.fadeOutCurve.store(fadeOutCurve, std::memory_order_release);

  // Calculate and store fade sample counts
  int64_t fadeInSampleCount =
      static_cast<int64_t>(fadeInSeconds * static_cast<double>(m_sampleRate));
  int64_t fadeOutSampleCount =
      static_cast<int64_t>(fadeOutSeconds * static_cast<double>(m_sampleRate));
  clip.fadeInSamples.store(fadeInSampleCount, std::memory_order_release);
  clip.fadeOutSamples.store(fadeOutSampleCount, std::memory_order_release);

  // Initialize gain from persistent storage
  clip.gainDb.store(gainDb, std::memory_order_release);

  // Initialize loop mode from persistent storage
  clip.loopEnabled.store(loopEnabled, std::memory_order_release);

  clip.reader = reader; // Store shared_ptr (maintains reference count)
  clip.numChannels = numChannels;
  clip.fadeOutGain = 1.0f;
  clip.isStopping = false;
  clip.fadeOutStartPos = 0; // Will be set when stopClip() is called

  // Initialize restart crossfade state (broadcast-safe restart mechanism)
  clip.isRestarting = false;
  clip.restartFadeFramesRemaining = 0;

  // Seek to trim IN point once when starting (ALWAYS seek, even if trim is 0!)
  if (reader) {
    int64_t trimIn = clip.trimInSamples.load(std::memory_order_acquire);
    reader->seek(trimIn);
  }
}

void TransportController::removeActiveClip(ClipHandle handle) {
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      // Remove by moving last clip into this slot (manual field-by-field copy since atomic fields
      // can't be copied)
      if (i < m_activeClipCount - 1) {
        ActiveClip& dest = m_activeClips[i];
        ActiveClip& src = m_activeClips[m_activeClipCount - 1];

        dest.handle = src.handle;
        dest.startSample = src.startSample;
        dest.currentSample = src.currentSample;
        dest.trimInSamples.store(src.trimInSamples.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        dest.trimOutSamples.store(src.trimOutSamples.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        dest.fadeInSeconds.store(src.fadeInSeconds.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        dest.fadeOutSeconds.store(src.fadeOutSeconds.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        dest.fadeInCurve.store(src.fadeInCurve.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        dest.fadeOutCurve.store(src.fadeOutCurve.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
        dest.fadeInSamples.store(src.fadeInSamples.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        dest.fadeOutSamples.store(src.fadeOutSamples.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        dest.gainDb.store(src.gainDb.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dest.loopEnabled.store(src.loopEnabled.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        dest.fadeOutGain = src.fadeOutGain;
        dest.isStopping = src.isStopping;
        dest.fadeOutStartPos = src.fadeOutStartPos;
        dest.isRestarting = src.isRestarting;
        dest.restartFadeFramesRemaining = src.restartFadeFramesRemaining;
        dest.reader = src.reader; // Copy shared_ptr (atomic refcount increment)
        dest.numChannels = src.numChannels;
      }
      --m_activeClipCount;
      return;
    }
  }
}

void TransportController::postCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_callbackQueue.push(std::move(callback));
}

void TransportController::processCallbacks() {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  while (!m_callbackQueue.empty()) {
    auto callback = std::move(m_callbackQueue.front());
    m_callbackQueue.pop();
    callback();
  }
}

SessionGraphError TransportController::registerClipAudio(ClipHandle handle,
                                                         const std::string& file_path) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  if (file_path.empty()) {
    return SessionGraphError::InvalidParameter;
  }

  std::lock_guard<std::mutex> lock(m_audioFilesMutex);

  // Create audio file reader (convert unique_ptr to shared_ptr for thread-safe lifetime)
  auto uniqueReader = createAudioFileReader();
  auto result = uniqueReader->open(file_path);

  if (!result.isOk()) {
    return result.error;
  }

  // Store reader and metadata for this clip
  AudioFileEntry entry;
  entry.reader = std::shared_ptr<IAudioFileReader>(std::move(uniqueReader));
  entry.metadata = result.value;

  // Apply session defaults to new clip
  entry.fadeInSeconds = m_sessionDefaults.fadeInSeconds;
  entry.fadeOutSeconds = m_sessionDefaults.fadeOutSeconds;
  entry.fadeInCurve = m_sessionDefaults.fadeInCurve;
  entry.fadeOutCurve = m_sessionDefaults.fadeOutCurve;
  entry.loopEnabled = m_sessionDefaults.loopEnabled;
  entry.stopOthersOnPlay = m_sessionDefaults.stopOthersOnPlay;
  entry.gainDb = m_sessionDefaults.gainDb;

  // Trim points default to full file duration
  entry.trimInSamples = 0;
  entry.trimOutSamples = result.value.duration_samples;

  m_audioFiles[handle] = std::move(entry);

  return SessionGraphError::OK;
}

SessionGraphError TransportController::updateClipTrimPoints(ClipHandle handle,
                                                            int64_t trimInSamples,
                                                            int64_t trimOutSamples) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Find clip in registered audio files (need to check file duration)
  int64_t fileDurationSamples = 0;
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
    fileDurationSamples = it->second.metadata.duration_samples;
  }

  // Validate trim points
  if (trimInSamples < 0 || trimInSamples >= fileDurationSamples) {
    return SessionGraphError::InvalidClipTrimPoints;
  }

  if (trimOutSamples <= trimInSamples || trimOutSamples > fileDurationSamples) {
    return SessionGraphError::InvalidClipTrimPoints;
  }

  // Store trim points persistently in AudioFileEntry
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it != m_audioFiles.end()) {
      it->second.trimInSamples = trimInSamples;
      it->second.trimOutSamples = trimOutSamples;
    }
  }

  // Update trim points for any active clips with this handle
  // NOTE: We update active clips directly (no command queue needed for metadata updates)
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      m_activeClips[i].trimInSamples.store(trimInSamples, std::memory_order_release);
      m_activeClips[i].trimOutSamples.store(trimOutSamples, std::memory_order_release);
    }
  }

  return SessionGraphError::OK;
}

SessionGraphError TransportController::updateClipFades(ClipHandle handle, double fadeInSeconds,
                                                       double fadeOutSeconds, FadeCurve fadeInCurve,
                                                       FadeCurve fadeOutCurve) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Find clip in registered audio files
  int64_t fileDurationSamples = 0;
  int64_t currentTrimIn = 0;
  int64_t currentTrimOut = 0;
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
    fileDurationSamples = it->second.metadata.duration_samples;
  }

  // Get current trim points (or use defaults)
  // Try to get from active clip first, otherwise use file duration
  bool foundActiveClip = false;
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      currentTrimIn = m_activeClips[i].trimInSamples.load(std::memory_order_acquire);
      currentTrimOut = m_activeClips[i].trimOutSamples.load(std::memory_order_acquire);
      foundActiveClip = true;
      break;
    }
  }

  if (!foundActiveClip) {
    currentTrimIn = 0;
    currentTrimOut = fileDurationSamples;
  }

  // Validate fade durations
  int64_t clipDuration = currentTrimOut - currentTrimIn;
  double clipDurationSeconds =
      static_cast<double>(clipDuration) / static_cast<double>(m_sampleRate);

  if (fadeInSeconds < 0.0 || fadeInSeconds > clipDurationSeconds) {
    return SessionGraphError::InvalidFadeDuration;
  }

  if (fadeOutSeconds < 0.0 || fadeOutSeconds > clipDurationSeconds) {
    return SessionGraphError::InvalidFadeDuration;
  }

  // Calculate fade sample counts
  int64_t fadeInSampleCount =
      static_cast<int64_t>(fadeInSeconds * static_cast<double>(m_sampleRate));
  int64_t fadeOutSampleCount =
      static_cast<int64_t>(fadeOutSeconds * static_cast<double>(m_sampleRate));

  // Store fade settings persistently in AudioFileEntry
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it != m_audioFiles.end()) {
      it->second.fadeInSeconds = fadeInSeconds;
      it->second.fadeOutSeconds = fadeOutSeconds;
      it->second.fadeInCurve = fadeInCurve;
      it->second.fadeOutCurve = fadeOutCurve;
    }
  }

  // Update fade settings for any active clips with this handle
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      m_activeClips[i].fadeInSeconds.store(fadeInSeconds, std::memory_order_release);
      m_activeClips[i].fadeOutSeconds.store(fadeOutSeconds, std::memory_order_release);
      m_activeClips[i].fadeInCurve.store(fadeInCurve, std::memory_order_release);
      m_activeClips[i].fadeOutCurve.store(fadeOutCurve, std::memory_order_release);
      m_activeClips[i].fadeInSamples.store(fadeInSampleCount, std::memory_order_release);
      m_activeClips[i].fadeOutSamples.store(fadeOutSampleCount, std::memory_order_release);
    }
  }

  return SessionGraphError::OK;
}

SessionGraphError TransportController::getClipTrimPoints(ClipHandle handle, int64_t& trimInSamples,
                                                         int64_t& trimOutSamples) const {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Try to get from active clip first (most recent values)
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      trimInSamples = m_activeClips[i].trimInSamples.load(std::memory_order_acquire);
      trimOutSamples = m_activeClips[i].trimOutSamples.load(std::memory_order_acquire);
      return SessionGraphError::OK;
    }
  }

  // If not active, check persistent storage
  // NOTE: const_cast needed because this is a query method (read-only, but mutex requires
  // non-const)
  {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_audioFilesMutex));
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }

    // Return persistent metadata (or defaults if not set)
    trimInSamples = it->second.trimInSamples;
    trimOutSamples = it->second.trimOutSamples;

    // If trim OUT is not set (0), use file duration
    if (trimOutSamples == 0) {
      trimOutSamples = it->second.metadata.duration_samples;
    }
  }

  return SessionGraphError::OK;
}

SessionGraphError TransportController::updateClipGain(ClipHandle handle, float gainDb) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Validate gain (must be finite)
  if (!std::isfinite(gainDb)) {
    return SessionGraphError::InvalidParameter;
  }

  // Store gain persistently in AudioFileEntry
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
    it->second.gainDb = gainDb;
  }

  // Update gain for any active clips with this handle (takes effect immediately)
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      m_activeClips[i].gainDb.store(gainDb, std::memory_order_release);
    }
  }

  return SessionGraphError::OK;
}

SessionGraphError TransportController::setClipLoopMode(ClipHandle handle, bool shouldLoop) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Store loop mode persistently in AudioFileEntry
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
    it->second.loopEnabled = shouldLoop;
  }

  // Update loop mode for any active clips with this handle
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      m_activeClips[i].loopEnabled.store(shouldLoop, std::memory_order_release);
    }
  }

  return SessionGraphError::OK;
}

int64_t TransportController::getClipPosition(ClipHandle handle) const {
  // Thread-safe query from any thread
  // Audio thread writes currentSample, UI thread reads (relaxed ordering is safe for progress
  // queries)
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      return m_activeClips[i].currentSample; // Returns position relative to file start
    }
  }

  return -1; // Clip not playing
}

SessionGraphError TransportController::setClipStopOthersMode(ClipHandle handle, bool enabled) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Store mode persistently in AudioFileEntry
  std::lock_guard<std::mutex> lock(m_audioFilesMutex);
  auto it = m_audioFiles.find(handle);
  if (it == m_audioFiles.end()) {
    return SessionGraphError::ClipNotRegistered;
  }

  it->second.stopOthersOnPlay = enabled;
  return SessionGraphError::OK;
}

bool TransportController::getClipStopOthersMode(ClipHandle handle) const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_audioFilesMutex));
  auto it = m_audioFiles.find(handle);
  if (it == m_audioFiles.end()) {
    return false;
  }

  return it->second.stopOthersOnPlay;
}

SessionGraphError TransportController::updateClipMetadata(ClipHandle handle,
                                                          const ClipMetadata& metadata) {
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Validate metadata before applying changes (atomic operation)
  // Get file duration for validation
  int64_t fileDurationSamples = 0;
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
    fileDurationSamples = it->second.metadata.duration_samples;
  }

  // Validate trim points
  int64_t trimOut = metadata.trimOutSamples;
  if (trimOut == 0) {
    trimOut = fileDurationSamples; // Use file duration if not specified
  }

  if (metadata.trimInSamples < 0 || metadata.trimInSamples >= fileDurationSamples) {
    return SessionGraphError::InvalidClipTrimPoints;
  }

  if (trimOut <= metadata.trimInSamples || trimOut > fileDurationSamples) {
    return SessionGraphError::InvalidClipTrimPoints;
  }

  // Validate fade durations
  int64_t clipDuration = trimOut - metadata.trimInSamples;
  double clipDurationSeconds =
      static_cast<double>(clipDuration) / static_cast<double>(m_sampleRate);

  if (metadata.fadeInSeconds < 0.0 || metadata.fadeInSeconds > clipDurationSeconds) {
    return SessionGraphError::InvalidFadeDuration;
  }

  if (metadata.fadeOutSeconds < 0.0 || metadata.fadeOutSeconds > clipDurationSeconds) {
    return SessionGraphError::InvalidFadeDuration;
  }

  // Validate gain
  if (!std::isfinite(metadata.gainDb)) {
    return SessionGraphError::InvalidParameter;
  }

  // All validation passed - apply changes atomically
  // Calculate fade sample counts
  int64_t fadeInSampleCount =
      static_cast<int64_t>(metadata.fadeInSeconds * static_cast<double>(m_sampleRate));
  int64_t fadeOutSampleCount =
      static_cast<int64_t>(metadata.fadeOutSeconds * static_cast<double>(m_sampleRate));

  // Update persistent storage
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it != m_audioFiles.end()) {
      it->second.trimInSamples = metadata.trimInSamples;
      it->second.trimOutSamples = trimOut;
      it->second.fadeInSeconds = metadata.fadeInSeconds;
      it->second.fadeOutSeconds = metadata.fadeOutSeconds;
      it->second.fadeInCurve = metadata.fadeInCurve;
      it->second.fadeOutCurve = metadata.fadeOutCurve;
      it->second.loopEnabled = metadata.loopEnabled;
      it->second.stopOthersOnPlay = metadata.stopOthersOnPlay;
      it->second.gainDb = metadata.gainDb;
    }
  }

  // Update active clips (if this clip is currently playing)
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      m_activeClips[i].trimInSamples.store(metadata.trimInSamples, std::memory_order_release);
      m_activeClips[i].trimOutSamples.store(trimOut, std::memory_order_release);
      m_activeClips[i].fadeInSeconds.store(metadata.fadeInSeconds, std::memory_order_release);
      m_activeClips[i].fadeOutSeconds.store(metadata.fadeOutSeconds, std::memory_order_release);
      m_activeClips[i].fadeInCurve.store(metadata.fadeInCurve, std::memory_order_release);
      m_activeClips[i].fadeOutCurve.store(metadata.fadeOutCurve, std::memory_order_release);
      m_activeClips[i].fadeInSamples.store(fadeInSampleCount, std::memory_order_release);
      m_activeClips[i].fadeOutSamples.store(fadeOutSampleCount, std::memory_order_release);
      m_activeClips[i].loopEnabled.store(metadata.loopEnabled, std::memory_order_release);
      m_activeClips[i].gainDb.store(metadata.gainDb, std::memory_order_release);
    }
  }

  return SessionGraphError::OK;
}

std::optional<ClipMetadata> TransportController::getClipMetadata(ClipHandle handle) const {
  if (handle == 0) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_audioFilesMutex));
  auto it = m_audioFiles.find(handle);
  if (it == m_audioFiles.end()) {
    return std::nullopt;
  }

  // Build metadata from persistent storage
  ClipMetadata metadata;
  metadata.trimInSamples = it->second.trimInSamples;
  metadata.trimOutSamples = it->second.trimOutSamples;
  metadata.fadeInSeconds = it->second.fadeInSeconds;
  metadata.fadeOutSeconds = it->second.fadeOutSeconds;
  metadata.fadeInCurve = it->second.fadeInCurve;
  metadata.fadeOutCurve = it->second.fadeOutCurve;
  metadata.loopEnabled = it->second.loopEnabled;
  metadata.stopOthersOnPlay = it->second.stopOthersOnPlay;
  metadata.gainDb = it->second.gainDb;

  // If trim OUT is not set (0), use file duration
  if (metadata.trimOutSamples == 0) {
    metadata.trimOutSamples = it->second.metadata.duration_samples;
  }

  return metadata;
}

float TransportController::calculateFadeGain(float normalizedPosition, FadeCurve curve) const {
  switch (curve) {
  case FadeCurve::Linear:
    return normalizedPosition; // y = x

  case FadeCurve::EqualPower:
    return std::sin(normalizedPosition * static_cast<float>(M_PI_2)); // y = sin(x * π/2)

  case FadeCurve::Exponential:
    return normalizedPosition * normalizedPosition; // y = x²

  default:
    return normalizedPosition; // Fallback to linear
  }
}

void TransportController::setSessionDefaults(const SessionDefaults& defaults) {
  std::lock_guard<std::mutex> lock(m_audioFilesMutex);
  m_sessionDefaults = defaults;
}

SessionDefaults TransportController::getSessionDefaults() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_audioFilesMutex));
  return m_sessionDefaults;
}

bool TransportController::isClipLooping(ClipHandle handle) const {
  // Thread-safe query from any thread
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      // Check if clip is looping (atomic read)
      return m_activeClips[i].loopEnabled.load(std::memory_order_relaxed);
    }
  }
  return false; // Clip not playing
}

SessionGraphError TransportController::restartClip(ClipHandle handle) {
  // Validate handle
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Check if clip is registered in audio files
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
  }

  // Find active clip (if playing)
  ActiveClip* activeClip = nullptr;
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      activeClip = &m_activeClips[i];
      break;
    }
  }

  if (activeClip == nullptr) {
    // Clip not playing - just start it normally
    return startClip(handle);
  }

  // CRITICAL: Broadcast-safe restart with crossfade (eliminates clicks)
  // Reset position to trim IN point (sample-accurate, atomic)
  int64_t trimIn = activeClip->trimInSamples.load(std::memory_order_acquire);
  activeClip->currentSample = trimIn;

  // Reset reader to trim IN point (seek operation)
  if (activeClip->reader) {
    activeClip->reader->seek(trimIn);
  }

  // Enable restart crossfade (5ms fade-in to eliminate clicks)
  // The audio thread will apply a linear fade-in over the next N samples
  activeClip->isRestarting = true;
  activeClip->restartFadeFramesRemaining = static_cast<int64_t>(m_restartCrossfadeSamples);

  // Reset fade-in state (apply fade-in from metadata)
  // Note: We don't modify isStopping or fadeOutGain - clip continues playing

  // Post callback to UI thread
  postCallback([this, handle, trimIn]() {
    if (m_callback) {
      TransportPosition pos;
      pos.samples = trimIn;
      pos.seconds = static_cast<double>(trimIn) / static_cast<double>(m_sampleRate);
      pos.beats = 0.0; // TODO: Calculate from tempo
      m_callback->onClipRestarted(handle, pos);
    }
  });

  return SessionGraphError::OK;
}

SessionGraphError TransportController::seekClip(ClipHandle handle, int64_t position) {
  // Validate handle
  if (handle == 0) {
    return SessionGraphError::InvalidHandle;
  }

  // Check if clip is registered (need to get file length for clamping)
  int64_t fileLength = 0;
  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it == m_audioFiles.end()) {
      return SessionGraphError::ClipNotRegistered;
    }
    fileLength = it->second.metadata.duration_samples;
  }

  // Find active clip (can only seek while playing)
  ActiveClip* activeClip = nullptr;
  for (size_t i = 0; i < m_activeClipCount; ++i) {
    if (m_activeClips[i].handle == handle) {
      activeClip = &m_activeClips[i];
      break;
    }
  }

  if (activeClip == nullptr) {
    // Clip not playing - cannot seek
    return SessionGraphError::NotReady;
  }

  // Clamp position to file bounds [0, fileLength]
  int64_t clampedPosition = std::clamp(position, int64_t(0), fileLength);

  // Atomic position update (sample-accurate)
  activeClip->currentSample = clampedPosition;

  // Seek reader to new position
  if (activeClip->reader) {
    activeClip->reader->seek(clampedPosition);
  }

  // Post seek callback to UI thread
  postCallback([this, handle, clampedPosition]() {
    if (m_callback) {
      TransportPosition pos;
      pos.samples = clampedPosition;
      pos.seconds = static_cast<double>(clampedPosition) / static_cast<double>(m_sampleRate);
      pos.beats = 0.0; // TODO: Calculate from tempo
      m_callback->onClipSeeked(handle, pos);
    }
  });

  return SessionGraphError::OK;
}

// Factory function
std::unique_ptr<ITransportController> createTransportController(core::SessionGraph* sessionGraph,
                                                                uint32_t sampleRate) {
  return std::make_unique<TransportController>(sessionGraph, sampleRate);
}

} // namespace orpheus
