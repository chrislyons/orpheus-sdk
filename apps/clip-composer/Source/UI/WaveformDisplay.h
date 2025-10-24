// SPDX-License-Identifier: MIT

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

//==============================================================================
/**
 * WaveformDisplay - Component for rendering audio waveforms
 *
 * Features:
 * - Efficient downsampled waveform rendering
 * - Visual trim point markers
 * - Interactive scrubbing (future)
 * - Support for stereo/mono files
 *
 * Threading:
 * - Waveform data generation happens on background thread
 * - Rendering happens on message thread (paint())
 * - Thread-safe via atomic flag and mutex
 */
class WaveformDisplay : public juce::Component {
public:
  //==============================================================================
  WaveformDisplay();
  ~WaveformDisplay() override = default;

  //==============================================================================
  // Load audio file and generate waveform data
  void setAudioFile(const juce::File& audioFile);

  // Set trim points (in samples) - updates visual markers
  void setTrimPoints(int64_t trimInSamples, int64_t trimOutSamples);

  // Set playhead position (in samples) - updates transport bar
  void setPlayheadPosition(int64_t samplePosition);

  // Zoom controls (4 levels: 1x, 2x, 4x, 8x)
  void setZoomLevel(int level); // 0=1x, 1=2x, 2=4x, 3=8x
  int getZoomLevel() const {
    return m_zoomLevel;
  }
  float getZoomFactor() const {
    return m_zoomFactor;
  }

  // Clear waveform data
  void clear();

  //==============================================================================
  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& event) override;
  void mouseDrag(const juce::MouseEvent& event) override;
  void mouseUp(const juce::MouseEvent& event) override;

  //==============================================================================
  // Callbacks for interactive waveform editing
  std::function<void(int64_t samples)> onLeftClick;                               // Set IN point
  std::function<void(int64_t samples)> onRightClick;                              // Set OUT point
  std::function<void(int64_t samples)> onMiddleClick;                             // Jump transport
  std::function<void(int64_t inSamples, int64_t outSamples)> onTrimPointsChanged; // Drag update

private:
  enum class DragHandle { None, TrimIn, TrimOut };

  DragHandle m_draggedHandle = DragHandle::None;
  bool isNearHandle(float mouseX, float handleX, float tolerance = 8.0f) const;
  //==============================================================================
  struct WaveformData {
    std::vector<float> minValues; // Min sample value per pixel column
    std::vector<float> maxValues; // Max sample value per pixel column
    int sampleRate = 48000;
    int numChannels = 2;
    int64_t totalSamples = 0;
    bool isValid = false;
  };

  void generateWaveformData(const juce::File& audioFile);
  void drawWaveform(juce::Graphics& g, const juce::Rectangle<float>& bounds);
  void drawTrimMarkers(juce::Graphics& g, const juce::Rectangle<float>& bounds);

  //==============================================================================
  WaveformData m_waveformData;
  int64_t m_trimInSamples = 0;
  int64_t m_trimOutSamples = 0;
  int64_t m_playheadPosition = 0;
  std::atomic<bool> m_isLoading{false};

  // Zoom state (4 levels: 1x, 2x, 4x, 8x)
  int m_zoomLevel = 0;       // 0=1x, 1=2x, 2=4x, 3=8x
  float m_zoomFactor = 1.0f; // Current zoom factor
  float m_zoomCenter = 0.5f; // Center of zoom (normalized 0-1)

  juce::CriticalSection m_dataLock; // Protects waveform data during background generation

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
