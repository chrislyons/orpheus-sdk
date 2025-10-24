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
  routingConfig.enable_clipping_protection = true;

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

  // DIAGNOSTIC: Write active clip count to file
  static int callbackCount = 0;
  if (callbackCount < 5) {
    FILE* f = fopen("/tmp/audio_callback.txt", "a");
    if (f) {
      fprintf(f, "processAudio() #%d: numFrames=%zu, activeClips=%zu\n", callbackCount, numFrames,
              m_activeClipCount);
      fclose(f);
    }
    callbackCount++;
  }

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

    // DIAGNOSTIC: Write clip state to file
    static int clipCheckCount = 0;
    if (clipCheckCount < 3) {
      FILE* f = fopen("/tmp/audio_callback.txt", "a");
      if (f) {
        fprintf(f, "Processing clip %zu: reader=%p, isOpen=%d\n", i, (void*)clip.audioReader,
                clip.audioReader ? clip.audioReader->isOpen() : 0);
        fclose(f);
      }
      clipCheckCount++;
    }

    // Skip if no audio file registered
    if (!clip.audioReader || !clip.audioReader->isOpen()) {
      FILE* f = fopen("/tmp/audio_callback.txt", "a");
      if (f) {
        fprintf(f, "SKIPPING clip %zu (no reader or not open)\n", i);
        fclose(f);
      }
      continue;
    }

    // Load trim and fade settings (atomic read for thread safety)
    int64_t trimIn = clip.trimInSamples.load(std::memory_order_acquire);
    int64_t trimOut = clip.trimOutSamples.load(std::memory_order_acquire);
    int64_t fadeInSampleCount = clip.fadeInSamples.load(std::memory_order_acquire);
    int64_t fadeOutSampleCount = clip.fadeOutSamples.load(std::memory_order_acquire);
    FadeCurve fadeInCurveType = clip.fadeInCurve.load(std::memory_order_acquire);
    FadeCurve fadeOutCurveType = clip.fadeOutCurve.load(std::memory_order_acquire);

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
    auto readResult = clip.audioReader->readSamples(clipReadBuffer, framesToRead);

    // DIAGNOSTIC: Write to file
    static int callCount = 0;
    if (callCount < 3) {
      FILE* f = fopen("/tmp/audio_callback.txt", "a");
      if (f) {
        fprintf(f, "TransportController: framesToRead=%zu, readResult.isOk=%d\n", framesToRead,
                readResult.isOk());
        fclose(f);
      }
      callCount++;
    }

    if (!readResult.isOk()) {
      FILE* f = fopen("/tmp/audio_callback.txt", "a");
      if (f) {
        fprintf(f, "ERROR: Read failed! Error code: %d\n", static_cast<int>(readResult.error));
        fclose(f);
      }
      continue;
    }

    size_t framesRead = readResult.value;

    // DIAGNOSTIC: Print samples from multiple buffers to see when audio starts
    static int sampleBufferCount = 0;
    if (sampleBufferCount < 50 && framesRead > 0) { // Log first 50 buffers
      FILE* f = fopen("/tmp/audio_callback.txt", "a");
      if (f) {
        fprintf(f, "BUFFER #%d: Read %zu frames, first 8 samples: ", sampleBufferCount, framesRead);
        for (size_t s = 0; s < std::min(size_t(8), framesRead * numFileChannels); ++s) {
          fprintf(f, "%.4f ", clipReadBuffer[s]);
        }
        fprintf(f, "\n");
        fclose(f);
      }
      sampleBufferCount++;
    }

    // Advance clip position by actual frames read (not buffer size!)
    clip.currentSample += static_cast<int64_t>(framesRead);

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

      // Apply stop fade-out if stopping
      if (clip.isStopping) {
        float stopFadeGain = clip.fadeOutGain;
        float fadeStep = 1.0f / static_cast<float>(m_fadeOutSamples);
        stopFadeGain -= (fadeStep * static_cast<float>(frame));
        gain *= std::max(0.0f, stopFadeGain); // Clamp to 0
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

    // DIAGNOSTIC: Log clipChannelBuffer samples for first active clip
    static int clipOutCount = 0;
    if (clipOutCount < 10 && i == 0 && framesRead > 0) {
      FILE* f = fopen("/tmp/audio_callback.txt", "a");
      if (f) {
        fprintf(f, "CLIP OUT #%d: First 8 samples from clipChannelBuffer[0]: ", clipOutCount);
        for (size_t s = 0; s < std::min(size_t(8), framesRead); ++s) {
          fprintf(f, "%.4f ", clipChannelBuffer[s]);
        }
        fprintf(f, "\n");
        fclose(f);
      }
      clipOutCount++;
    }
  }

  // DIAGNOSTIC: Log before routing
  static int beforeRoutingCount = 0;
  if (beforeRoutingCount < 3) {
    FILE* f = fopen("/tmp/audio_callback.txt", "a");
    if (f) {
      fprintf(f, "BEFORE ROUTING: activeClips=%zu, clipChannelPointers=%p, outputBuffers=%p\n",
              m_activeClipCount, (void*)m_clipChannelPointers.data(), (void*)outputBuffers);
      if (m_activeClipCount > 0) {
        fprintf(f, "  Clip 0 buffer first 8 samples: ");
        for (size_t s = 0; s < 8; ++s) {
          fprintf(f, "%.4f ", m_clipChannelBuffers[0][s]);
        }
        fprintf(f, "\n");
      }
      fclose(f);
    }
    beforeRoutingCount++;
  }

  // Process routing matrix: clips → groups → master output
  m_routingMatrix->processRouting(const_cast<const float**>(m_clipChannelPointers.data()),
                                  outputBuffers, static_cast<uint32_t>(numFrames));

  // Update clips
  size_t i = 0;
  while (i < m_activeClipCount) {
    ActiveClip& clip = m_activeClips[i];

    // Apply fade-out if stopping
    if (clip.isStopping) {
      float fadeStep = 1.0f / static_cast<float>(m_fadeOutSamples);
      clip.fadeOutGain -= fadeStep * static_cast<float>(numFrames);

      if (clip.fadeOutGain <= 0.0f) {
        // Fade complete, remove clip
        postCallback([this, handle = clip.handle, pos = getCurrentPosition()]() {
          if (m_callback) {
            m_callback->onClipStopped(handle, pos);
          }
        });

        removeActiveClip(clip.handle);
        continue; // Don't increment i, we just removed this clip
      }
    }

    // Check if clip reached trim OUT point
    int64_t clipTrimOut = clip.trimOutSamples.load(std::memory_order_acquire);
    if (clip.currentSample >= clipTrimOut) {
      // Check if clip should loop
      bool shouldLoop = clip.loopEnabled.load(std::memory_order_acquire);

      if (shouldLoop && clip.audioReader) {
        // Loop: seek back to trim IN point
        int64_t trimIn = clip.trimInSamples.load(std::memory_order_acquire);
        clip.audioReader->seek(trimIn);
        clip.currentSample = trimIn;

        // Post loop callback
        postCallback([this, handle = clip.handle, pos = getCurrentPosition()]() {
          if (m_callback) {
            m_callback->onClipLooped(handle, pos);
          }
        });

        // Continue playback (don't remove clip, don't increment i)
        ++i;
      } else {
        // Stop the clip
        postCallback([this, handle = clip.handle, pos = getCurrentPosition()]() {
          if (m_callback) {
            m_callback->onClipStopped(handle, pos);
          }
        });

        removeActiveClip(clip.handle);
        continue; // Don't increment i (clip was removed)
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
      }
    } break;

    case TransportCommand::Type::StopAll:
      for (size_t i = 0; i < m_activeClipCount; ++i) {
        m_activeClips[i].isStopping = true;
        m_activeClips[i].fadeOutGain = 1.0f;
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
  IAudioFileReader* reader = nullptr;
  uint16_t numChannels = 2;         // Default stereo
  int64_t totalFrames = 48000 * 10; // Default 10 seconds

  // Persistent metadata from storage
  int64_t trimInSamples = 0;
  int64_t trimOutSamples = 0;
  double fadeInSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  FadeCurve fadeInCurve = FadeCurve::Linear;
  FadeCurve fadeOutCurve = FadeCurve::Linear;
  float gainDb = 0.0f;
  bool loopEnabled = false;

  {
    std::lock_guard<std::mutex> lock(m_audioFilesMutex);
    auto it = m_audioFiles.find(handle);
    if (it != m_audioFiles.end()) {
      reader = it->second.reader.get();
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

      // If trim OUT is not set (0), use file duration
      if (trimOutSamples == 0) {
        trimOutSamples = totalFrames;
      }
    }
  }

  // DIAGNOSTIC: Write to file
  FILE* f = fopen("/tmp/audio_callback.txt", "a");
  if (f) {
    fprintf(f, "addActiveClip(handle=%llu): reader=%p, isOpen=%d, channels=%d, frames=%lld\n",
            (unsigned long long)handle, (void*)reader, reader ? reader->isOpen() : 0, numChannels,
            (long long)totalFrames);
    fclose(f);
  }

  // If no audio file registered, we'll play silence (reader will be nullptr)

  // Initialize clip with persistent metadata from storage
  ActiveClip& clip = m_activeClips[m_activeClipCount++];
  clip.handle = handle;
  clip.startSample = m_currentSample.load(std::memory_order_relaxed);
  clip.currentSample = 0;

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

  clip.audioReader = reader;
  clip.numChannels = numChannels;
  clip.fadeOutGain = 1.0f;
  clip.isStopping = false;

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
        dest.audioReader = src.audioReader;
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

  // Create audio file reader
  auto reader = createAudioFileReader();
  auto result = reader->open(file_path);

  if (!result.isOk()) {
    return result.error;
  }

  // Store reader and metadata for this clip
  AudioFileEntry entry;
  entry.reader = std::move(reader);
  entry.metadata = result.value;
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

// Factory function
std::unique_ptr<ITransportController> createTransportController(core::SessionGraph* sessionGraph,
                                                                uint32_t sampleRate) {
  return std::make_unique<TransportController>(sessionGraph, sampleRate);
}

} // namespace orpheus
