// SPDX-License-Identifier: MIT

#include "ClipEditDialog.h"

//==============================================================================
ClipEditDialog::ClipEditDialog(AudioEngine* audioEngine) : m_audioEngine(audioEngine) {
  // Create preview player with AudioEngine reference (for Cue Buss allocation)
  m_previewPlayer = std::make_unique<PreviewPlayer>(m_audioEngine);

  // Build Phase 1 UI (basic metadata)
  buildPhase1UI();

  // Build Phase 2 UI (In/Out points)
  buildPhase2UI();

  // Build Phase 3 UI (Fade times)
  buildPhase3UI();

  setSize(700, 800); // Expanded for all phases
}

ClipEditDialog::~ClipEditDialog() {
  // CRITICAL: Clear all callbacks BEFORE PreviewPlayer is destroyed
  // Prevents heap-use-after-free when audio thread tries to access destroyed dialog
  if (m_previewPlayer) {
    m_previewPlayer->onPositionChanged = nullptr;
    m_previewPlayer->onPlaybackStopped = nullptr;
    m_previewPlayer->stop(); // Ensure Cue Buss is stopped
  }
  // PreviewPlayer destructor will safely release Cue Buss (via AudioEngine)
}

//==============================================================================
void ClipEditDialog::setClipMetadata(const ClipMetadata& metadata) {
  m_metadata = metadata;

  // Update UI controls
  if (m_nameEditor)
    m_nameEditor->setText(m_metadata.displayName, false);

  if (m_filePathEditor)
    m_filePathEditor->setText(m_metadata.filePath, false);

  if (m_groupComboBox)
    m_groupComboBox->setSelectedId(m_metadata.clipGroup + 1, juce::dontSendNotification);

  // Set color combo box based on metadata color
  if (m_colorComboBox) {
    // Map color to combo box item (simplified for now)
    if (m_metadata.color == juce::Colour(0xffe74c3c))
      m_colorComboBox->setSelectedId(1); // Red
    else if (m_metadata.color == juce::Colour(0xfff39c12))
      m_colorComboBox->setSelectedId(2); // Orange
    else if (m_metadata.color == juce::Colour(0xfff1c40f))
      m_colorComboBox->setSelectedId(3); // Yellow
    else if (m_metadata.color == juce::Colour(0xff2ecc71))
      m_colorComboBox->setSelectedId(4); // Green
    else if (m_metadata.color == juce::Colour(0xff1abc9c))
      m_colorComboBox->setSelectedId(5); // Cyan
    else if (m_metadata.color == juce::Colour(0xff3498db))
      m_colorComboBox->setSelectedId(6); // Blue
    else if (m_metadata.color == juce::Colour(0xff9b59b6))
      m_colorComboBox->setSelectedId(7); // Purple
    else if (m_metadata.color == juce::Colour(0xffff69b4))
      m_colorComboBox->setSelectedId(8); // Pink
    else
      m_colorComboBox->setSelectedId(1); // Default to Red
  }

  // Update trim info label
  updateTrimInfoLabel();

  // Update file info panel (SpotOn-style)
  if (m_fileInfoPanel) {
    double durationSeconds =
        static_cast<double>(m_metadata.durationSamples) / m_metadata.sampleRate;
    juce::String formatName =
        m_metadata.filePath.fromLastOccurrenceOf(".", false, true).toUpperCase();
    if (formatName.isEmpty())
      formatName = "Unknown";

    juce::String infoText = juce::String::formatted(
        "  Channels: %d  |  Sample Rate: %d Hz  |  Duration: %.2fs  |  Format: %s",
        m_metadata.numChannels, m_metadata.sampleRate, durationSeconds, formatName.toRawUTF8());
    m_fileInfoPanel->setText(infoText, juce::dontSendNotification);
  }

  // Load waveform display and preview player
  if (m_waveformDisplay && m_metadata.filePath.isNotEmpty()) {
    juce::File audioFile(m_metadata.filePath);
    if (audioFile.existsAsFile()) {
      m_waveformDisplay->setAudioFile(audioFile);
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples > 0
                                                                     ? m_metadata.trimOutSamples
                                                                     : m_metadata.durationSamples);

      // Load into preview player
      if (m_previewPlayer) {
        m_previewPlayer->loadFile(audioFile);
        m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples > 0
                                                                     ? m_metadata.trimOutSamples
                                                                     : m_metadata.durationSamples);
        m_previewPlayer->setFades(m_metadata.fadeInSeconds, m_metadata.fadeOutSeconds,
                                  m_metadata.fadeInCurve, m_metadata.fadeOutCurve);
      }
    }
  }

  // Phase 3: Initialize fade combos
  if (m_fadeInCombo) {
    // Map fade time to combo ID
    if (m_metadata.fadeInSeconds <= 0.05)
      m_fadeInCombo->setSelectedId(1);
    else if (m_metadata.fadeInSeconds <= 0.15)
      m_fadeInCombo->setSelectedId(2);
    else if (m_metadata.fadeInSeconds <= 0.25)
      m_fadeInCombo->setSelectedId(3);
    else if (m_metadata.fadeInSeconds <= 0.4)
      m_fadeInCombo->setSelectedId(4);
    else if (m_metadata.fadeInSeconds <= 0.75)
      m_fadeInCombo->setSelectedId(5);
    else if (m_metadata.fadeInSeconds <= 1.25)
      m_fadeInCombo->setSelectedId(6);
    else if (m_metadata.fadeInSeconds <= 1.75)
      m_fadeInCombo->setSelectedId(7);
    else if (m_metadata.fadeInSeconds <= 2.5)
      m_fadeInCombo->setSelectedId(8);
    else
      m_fadeInCombo->setSelectedId(9);
  }
  if (m_fadeOutCombo) {
    // Map fade time to combo ID
    if (m_metadata.fadeOutSeconds <= 0.05)
      m_fadeOutCombo->setSelectedId(1);
    else if (m_metadata.fadeOutSeconds <= 0.15)
      m_fadeOutCombo->setSelectedId(2);
    else if (m_metadata.fadeOutSeconds <= 0.25)
      m_fadeOutCombo->setSelectedId(3);
    else if (m_metadata.fadeOutSeconds <= 0.4)
      m_fadeOutCombo->setSelectedId(4);
    else if (m_metadata.fadeOutSeconds <= 0.75)
      m_fadeOutCombo->setSelectedId(5);
    else if (m_metadata.fadeOutSeconds <= 1.25)
      m_fadeOutCombo->setSelectedId(6);
    else if (m_metadata.fadeOutSeconds <= 1.75)
      m_fadeOutCombo->setSelectedId(7);
    else if (m_metadata.fadeOutSeconds <= 2.5)
      m_fadeOutCombo->setSelectedId(8);
    else
      m_fadeOutCombo->setSelectedId(9);
  }

  // Set fade curve combos
  if (m_fadeInCurveCombo) {
    if (m_metadata.fadeInCurve == "Linear")
      m_fadeInCurveCombo->setSelectedId(1, juce::dontSendNotification);
    else if (m_metadata.fadeInCurve == "EqualPower")
      m_fadeInCurveCombo->setSelectedId(2, juce::dontSendNotification);
    else if (m_metadata.fadeInCurve == "Exponential")
      m_fadeInCurveCombo->setSelectedId(3, juce::dontSendNotification);
  }

  if (m_fadeOutCurveCombo) {
    if (m_metadata.fadeOutCurve == "Linear")
      m_fadeOutCurveCombo->setSelectedId(1, juce::dontSendNotification);
    else if (m_metadata.fadeOutCurve == "EqualPower")
      m_fadeOutCurveCombo->setSelectedId(2, juce::dontSendNotification);
    else if (m_metadata.fadeOutCurve == "Exponential")
      m_fadeOutCurveCombo->setSelectedId(3, juce::dontSendNotification);
  }
}

juce::String ClipEditDialog::samplesToTimeString(int64_t samples, int sampleRate) {
  if (sampleRate <= 0)
    return "00:00:00.00";

  double totalSeconds = static_cast<double>(samples) / sampleRate;
  int hours = static_cast<int>(totalSeconds / 3600);
  int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);
  int seconds = static_cast<int>(totalSeconds) % 60;

  // Frames: 75fps (SpotOn standard)
  double fractionalSeconds = totalSeconds - static_cast<int>(totalSeconds);
  int frames = static_cast<int>(fractionalSeconds * 75.0);

  // Format as HH:MM:SS.FF
  return juce::String::formatted("%02d:%02d:%02d.%02d", hours, minutes, seconds, frames);
}

int64_t ClipEditDialog::timeStringToSamples(const juce::String& timeStr, int sampleRate) {
  // Parse HH:MM:SS.FF format (75fps)
  // Split by ':' first
  auto parts = juce::StringArray::fromTokens(timeStr, ":", "");
  if (parts.size() < 2)
    return 0;

  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frames = 0;

  if (parts.size() == 3) {
    // HH:MM:SS.FF
    hours = parts[0].getIntValue();
    minutes = parts[1].getIntValue();
    // Split seconds and frames by '.'
    auto secParts = juce::StringArray::fromTokens(parts[2], ".", "");
    seconds = secParts[0].getIntValue();
    if (secParts.size() > 1)
      frames = secParts[1].getIntValue();
  } else if (parts.size() == 2) {
    // MM:SS.FF (no hours)
    minutes = parts[0].getIntValue();
    auto secParts = juce::StringArray::fromTokens(parts[1], ".", "");
    seconds = secParts[0].getIntValue();
    if (secParts.size() > 1)
      frames = secParts[1].getIntValue();
  }

  // Convert to total seconds (75fps)
  double totalSeconds = hours * 3600.0 + minutes * 60.0 + seconds + (frames / 75.0);
  return static_cast<int64_t>(totalSeconds * sampleRate);
}

void ClipEditDialog::updateTrimInfoLabel() {
  if (!m_trimInfoLabel)
    return;

  int64_t trimmedSamples = m_metadata.trimOutSamples - m_metadata.trimInSamples;

  if (trimmedSamples < 0) {
    m_trimInfoLabel->setText("Invalid trim range", juce::dontSendNotification);
    return;
  }

  if (m_metadata.sampleRate > 0) {
    // Use HH:MM:SS.FF format for duration display (SpotOn standard)
    juce::String durationText =
        "Duration: " + samplesToTimeString(trimmedSamples, m_metadata.sampleRate);
    m_trimInfoLabel->setText(durationText, juce::dontSendNotification);
  }

  // Update time editors
  if (m_trimInTimeEditor) {
    m_trimInTimeEditor->setText(
        samplesToTimeString(m_metadata.trimInSamples, m_metadata.sampleRate), false);
  }
  if (m_trimOutTimeEditor) {
    m_trimOutTimeEditor->setText(
        samplesToTimeString(m_metadata.trimOutSamples, m_metadata.sampleRate), false);
  }
}

//==============================================================================
void ClipEditDialog::buildPhase1UI() {
  // Clip Name
  m_nameLabel = std::make_unique<juce::Label>("nameLabel", "Clip Name:");
  m_nameLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_nameLabel.get());

  m_nameEditor = std::make_unique<juce::TextEditor>();
  m_nameEditor->setFont(juce::Font("Inter", 14.0f, juce::Font::plain));
  m_nameEditor->onTextChange = [this]() { m_metadata.displayName = m_nameEditor->getText(); };
  addAndMakeVisible(m_nameEditor.get());

  // File Path (read-only)
  m_filePathLabel = std::make_unique<juce::Label>("filePathLabel", "File Path:");
  m_filePathLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_filePathLabel.get());

  m_filePathEditor = std::make_unique<juce::TextEditor>();
  m_filePathEditor->setFont(juce::Font("Inter", 12.0f, juce::Font::plain));
  m_filePathEditor->setReadOnly(true);
  m_filePathEditor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
  addAndMakeVisible(m_filePathEditor.get());

  // Color
  m_colorLabel = std::make_unique<juce::Label>("colorLabel", "Color:");
  m_colorLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_colorLabel.get());

  m_colorComboBox = std::make_unique<juce::ComboBox>();
  m_colorComboBox->addItem("Red", 1);
  m_colorComboBox->addItem("Orange", 2);
  m_colorComboBox->addItem("Yellow", 3);
  m_colorComboBox->addItem("Green", 4);
  m_colorComboBox->addItem("Cyan", 5);
  m_colorComboBox->addItem("Blue", 6);
  m_colorComboBox->addItem("Purple", 7);
  m_colorComboBox->addItem("Pink", 8);
  m_colorComboBox->onChange = [this]() {
    int colorId = m_colorComboBox->getSelectedId();
    switch (colorId) {
    case 1:
      m_metadata.color = juce::Colour(0xffe74c3c);
      break; // Red
    case 2:
      m_metadata.color = juce::Colour(0xfff39c12);
      break; // Orange
    case 3:
      m_metadata.color = juce::Colour(0xfff1c40f);
      break; // Yellow
    case 4:
      m_metadata.color = juce::Colour(0xff2ecc71);
      break; // Green
    case 5:
      m_metadata.color = juce::Colour(0xff1abc9c);
      break; // Cyan
    case 6:
      m_metadata.color = juce::Colour(0xff3498db);
      break; // Blue
    case 7:
      m_metadata.color = juce::Colour(0xff9b59b6);
      break; // Purple
    case 8:
      m_metadata.color = juce::Colour(0xffff69b4);
      break; // Pink
    }
  };
  addAndMakeVisible(m_colorComboBox.get());

  // Clip Group
  m_groupLabel = std::make_unique<juce::Label>("groupLabel", "Clip Group:");
  m_groupLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_groupLabel.get());

  m_groupComboBox = std::make_unique<juce::ComboBox>();
  m_groupComboBox->addItem("Group 1 (Blue)", 1);
  m_groupComboBox->addItem("Group 2 (Green)", 2);
  m_groupComboBox->addItem("Group 3 (Orange)", 3);
  m_groupComboBox->addItem("Group 4 (Red)", 4);
  m_groupComboBox->onChange = [this]() {
    m_metadata.clipGroup = m_groupComboBox->getSelectedId() - 1; // 0-3
  };
  addAndMakeVisible(m_groupComboBox.get());

  // Dialog buttons
  m_okButton = std::make_unique<juce::TextButton>("OK");
  m_okButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2ecc71)); // Green
  m_okButton->onClick = [this]() {
    if (onOkClicked)
      onOkClicked(m_metadata);
  };
  addAndMakeVisible(m_okButton.get());

  m_cancelButton = std::make_unique<juce::TextButton>("Cancel");
  m_cancelButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff95a5a6)); // Grey
  m_cancelButton->onClick = [this]() {
    if (onCancelClicked)
      onCancelClicked();
  };
  addAndMakeVisible(m_cancelButton.get());
}

void ClipEditDialog::buildPhase2UI() {
  // Waveform Display (real component)
  m_waveformDisplay = std::make_unique<WaveformDisplay>();
  addAndMakeVisible(m_waveformDisplay.get());

  // Preview Transport Controls
  m_playButton = std::make_unique<juce::TextButton>("Play");
  m_playButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2ecc71)); // Green
  m_playButton->onClick = [this]() {
    if (m_previewPlayer) {
      // Always restart playback from IN point (SpotOn behavior)
      m_previewPlayer->stop();
      m_previewPlayer->play();
      DBG("ClipEditDialog: Preview playback restarted");
    }
  };
  addAndMakeVisible(m_playButton.get());

  m_stopButton = std::make_unique<juce::TextButton>("Stop");
  m_stopButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe74c3c)); // Red
  m_stopButton->onClick = [this]() {
    if (m_previewPlayer) {
      m_previewPlayer->stop();
      DBG("ClipEditDialog: Preview playback stopped");
    }
  };
  addAndMakeVisible(m_stopButton.get());

  m_loopButton = std::make_unique<juce::ToggleButton>("Loop");
  m_loopButton->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  m_loopButton->onClick = [this]() {
    if (m_previewPlayer) {
      bool loopEnabled = m_loopButton->getToggleState();
      m_previewPlayer->setLoopEnabled(loopEnabled);
      DBG("ClipEditDialog: Preview loop " << (loopEnabled ? "enabled" : "disabled"));
    }
  };
  addAndMakeVisible(m_loopButton.get());

  m_transportPositionLabel = std::make_unique<juce::Label>("posLabel", "00:00:00");
  m_transportPositionLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::plain));
  m_transportPositionLabel->setJustificationType(juce::Justification::centred);
  addAndMakeVisible(m_transportPositionLabel.get());

  // Wire up preview player callbacks
  if (m_previewPlayer) {
    m_previewPlayer->onPositionChanged = [this](int64_t samplePosition) {
      // Update position label
      if (m_transportPositionLabel) {
        juce::String timeString = samplesToTimeString(samplePosition, m_metadata.sampleRate);
        m_transportPositionLabel->setText(timeString, juce::dontSendNotification);
      }

      // Update waveform playhead
      if (m_waveformDisplay) {
        m_waveformDisplay->setPlayheadPosition(samplePosition);
      }
    };

    m_previewPlayer->onPlaybackStopped = [this]() {
      DBG("ClipEditDialog: Preview playback stopped (reached end or manual stop)");
    };
  }

  // Set up waveform click handlers for LOOP functionality
  m_waveformDisplay->onLeftClick = [this](int64_t samples) {
    // Left click: Set IN point
    // Validate: IN must be < OUT
    int64_t newInPoint = samples;
    if (newInPoint >= m_metadata.trimOutSamples) {
      newInPoint = std::max(int64_t(0), m_metadata.trimOutSamples - (m_metadata.sampleRate / 75));
    }

    m_metadata.trimInSamples = newInPoint;
    updateTrimInfoLabel();
    m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);

    // Update preview player trim points
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    DBG("ClipEditDialog: Set IN point to sample " << newInPoint);
  };

  m_waveformDisplay->onRightClick = [this](int64_t samples) {
    // Right click: Set OUT point
    // Validate: OUT must be > IN
    int64_t newOutPoint = samples;
    if (newOutPoint <= m_metadata.trimInSamples) {
      newOutPoint = std::min(m_metadata.durationSamples,
                             m_metadata.trimInSamples + (m_metadata.sampleRate / 75));
    }

    m_metadata.trimOutSamples = newOutPoint;
    updateTrimInfoLabel();
    m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);

    // Update preview player trim points
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    DBG("ClipEditDialog: Set OUT point to sample " << newOutPoint);
  };

  m_waveformDisplay->onMiddleClick = [this](int64_t samples) {
    // Middle click: Jump transport (future implementation)
    DBG("ClipEditDialog: Transport jump to sample " << samples << " (not yet implemented)");
    // TODO: Implement transport jump when AudioEngine preview functionality is available
  };

  m_waveformDisplay->onTrimPointsChanged = [this](int64_t inSamples, int64_t outSamples) {
    // Update metadata and UI when handles are dragged
    m_metadata.trimInSamples = inSamples;
    m_metadata.trimOutSamples = outSamples;
    updateTrimInfoLabel();

    // Update preview player trim points
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(inSamples, outSamples);
    }
  };

  // Zoom buttons (4 levels: 1x, 2x, 4x, 8x)
  m_zoom1xButton = std::make_unique<juce::TextButton>("1x");
  m_zoom1xButton->setToggleState(true, juce::dontSendNotification); // Default active
  m_zoom1xButton->onClick = [this]() {
    m_waveformDisplay->setZoomLevel(0);
    m_zoom1xButton->setToggleState(true, juce::dontSendNotification);
    m_zoom2xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom4xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom8xButton->setToggleState(false, juce::dontSendNotification);
  };
  addAndMakeVisible(m_zoom1xButton.get());

  m_zoom2xButton = std::make_unique<juce::TextButton>("2x");
  m_zoom2xButton->onClick = [this]() {
    m_waveformDisplay->setZoomLevel(1);
    m_zoom1xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom2xButton->setToggleState(true, juce::dontSendNotification);
    m_zoom4xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom8xButton->setToggleState(false, juce::dontSendNotification);
  };
  addAndMakeVisible(m_zoom2xButton.get());

  m_zoom4xButton = std::make_unique<juce::TextButton>("4x");
  m_zoom4xButton->onClick = [this]() {
    m_waveformDisplay->setZoomLevel(2);
    m_zoom1xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom2xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom4xButton->setToggleState(true, juce::dontSendNotification);
    m_zoom8xButton->setToggleState(false, juce::dontSendNotification);
  };
  addAndMakeVisible(m_zoom4xButton.get());

  m_zoom8xButton = std::make_unique<juce::TextButton>("8x");
  m_zoom8xButton->onClick = [this]() {
    m_waveformDisplay->setZoomLevel(3);
    m_zoom1xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom2xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom4xButton->setToggleState(false, juce::dontSendNotification);
    m_zoom8xButton->setToggleState(true, juce::dontSendNotification);
  };
  addAndMakeVisible(m_zoom8xButton.get());

  // Trim In Point
  m_trimInLabel = std::make_unique<juce::Label>("trimInLabel", "Trim In:");
  m_trimInLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_trimInLabel.get());

  // Time editor (MM:SS:FF - SpotOn format, 75fps)
  m_trimInTimeEditor = std::make_unique<juce::TextEditor>();
  m_trimInTimeEditor->setFont(juce::Font("Inter", 12.0f, juce::Font::plain));
  m_trimInTimeEditor->setText("00:00:00", false);
  m_trimInTimeEditor->onReturnKey = [this]() {
    int64_t newInPoint = timeStringToSamples(m_trimInTimeEditor->getText(), m_metadata.sampleRate);

    // Validate: IN must be < OUT
    if (newInPoint >= m_metadata.trimOutSamples) {
      newInPoint = std::max(int64_t(0), m_metadata.trimOutSamples - (m_metadata.sampleRate / 75));
    }

    m_metadata.trimInSamples = newInPoint;
    updateTrimInfoLabel();

    // Update waveform display
    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Update preview player trim points and restart for audition
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      m_previewPlayer->play(); // Restart to audition new IN point
    }
  };
  addAndMakeVisible(m_trimInTimeEditor.get());

  // Nudge buttons (< and > for rapid audition)
  m_trimInDecButton = std::make_unique<juce::TextButton>("<");
  m_trimInDecButton->onClick = [this]() {
    // Decrement by 1 tick (1/75 second) and restart preview
    int64_t decrement = m_metadata.sampleRate / 75;
    m_metadata.trimInSamples = std::max(int64_t(0), m_metadata.trimInSamples - decrement);

    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Restart preview for instant audition
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      m_previewPlayer->stop(); // Stop first to force restart
      m_previewPlayer->play(); // Restart from new IN point
    }
  };
  addAndMakeVisible(m_trimInDecButton.get());

  m_trimInIncButton = std::make_unique<juce::TextButton>(">");
  m_trimInIncButton->onClick = [this]() {
    // Increment by 1 tick (1/75 second) and restart preview
    int64_t increment = m_metadata.sampleRate / 75;
    m_metadata.trimInSamples = std::min(m_metadata.trimOutSamples - (m_metadata.sampleRate / 75),
                                        m_metadata.trimInSamples + increment);

    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Restart preview for instant audition
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      m_previewPlayer->stop(); // Stop first to force restart
      m_previewPlayer->play(); // Restart from new IN point
    }
  };
  addAndMakeVisible(m_trimInIncButton.get());

  // HOLD button for IN point (capture current playback position - SpotOn-inspired)
  m_trimInHoldButton = std::make_unique<juce::TextButton>("HOLD");
  m_trimInHoldButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3498db)); // Blue
  m_trimInHoldButton->onClick = [this]() {
    if (m_previewPlayer) {
      int64_t currentPos = m_previewPlayer->getCurrentPosition();

      // Validate: IN must be < OUT
      if (currentPos >= m_metadata.trimOutSamples) {
        currentPos = std::max(int64_t(0), m_metadata.trimOutSamples - (m_metadata.sampleRate / 75));
      }

      m_metadata.trimInSamples = currentPos;
      updateTrimInfoLabel();

      if (m_waveformDisplay) {
        m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      }

      // Update preview player and restart
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      m_previewPlayer->play();

      DBG("ClipEditDialog: HOLD - Set IN point to current position " << currentPos);
    }
  };
  addAndMakeVisible(m_trimInHoldButton.get());

  // CLEAR button for IN point (reset to 0)
  m_trimInClearButton = std::make_unique<juce::TextButton>("CLR");
  m_trimInClearButton->onClick = [this]() {
    m_metadata.trimInSamples = 0;

    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Update preview player
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    DBG("ClipEditDialog: IN point cleared to 0");
  };
  addAndMakeVisible(m_trimInClearButton.get());

  // Trim Out Point
  m_trimOutLabel = std::make_unique<juce::Label>("trimOutLabel", "Trim Out:");
  m_trimOutLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_trimOutLabel.get());

  // Time editor (MM:SS:FF - SpotOn format, 75fps)
  m_trimOutTimeEditor = std::make_unique<juce::TextEditor>();
  m_trimOutTimeEditor->setFont(juce::Font("Inter", 12.0f, juce::Font::plain));
  m_trimOutTimeEditor->setText("00:00:00", false);
  m_trimOutTimeEditor->onReturnKey = [this]() {
    int64_t newOutPoint =
        timeStringToSamples(m_trimOutTimeEditor->getText(), m_metadata.sampleRate);

    // Validate: OUT must be > IN
    if (newOutPoint <= m_metadata.trimInSamples) {
      newOutPoint = std::min(m_metadata.durationSamples,
                             m_metadata.trimInSamples + (m_metadata.sampleRate / 75));
    }

    m_metadata.trimOutSamples = newOutPoint;
    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Update preview player
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
  };
  addAndMakeVisible(m_trimOutTimeEditor.get());

  // Nudge buttons
  m_trimOutDecButton = std::make_unique<juce::TextButton>("<");
  m_trimOutDecButton->onClick = [this]() {
    // Decrement by 1 tick (1/75 second) - NO restart (SpotOn behavior)
    int64_t decrement = m_metadata.sampleRate / 75;
    m_metadata.trimOutSamples = std::max(m_metadata.trimInSamples + (m_metadata.sampleRate / 75),
                                         m_metadata.trimOutSamples - decrement);

    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Update preview player trim points WITHOUT restarting playback
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
  };
  addAndMakeVisible(m_trimOutDecButton.get());

  m_trimOutIncButton = std::make_unique<juce::TextButton>(">");
  m_trimOutIncButton->onClick = [this]() {
    // Increment by 1 tick (1/75 second) - NO restart (SpotOn behavior)
    int64_t increment = m_metadata.sampleRate / 75;
    m_metadata.trimOutSamples =
        std::min(m_metadata.durationSamples, m_metadata.trimOutSamples + increment);

    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Update preview player trim points WITHOUT restarting playback
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
  };
  addAndMakeVisible(m_trimOutIncButton.get());

  // HOLD button for OUT point (capture current playback position - SpotOn-inspired)
  m_trimOutHoldButton = std::make_unique<juce::TextButton>("HOLD");
  m_trimOutHoldButton->setColour(juce::TextButton::buttonColourId,
                                 juce::Colour(0xff3498db)); // Blue
  m_trimOutHoldButton->onClick = [this]() {
    if (m_previewPlayer) {
      int64_t currentPos = m_previewPlayer->getCurrentPosition();

      // Validate: OUT must be > IN
      if (currentPos <= m_metadata.trimInSamples) {
        currentPos = std::min(m_metadata.durationSamples,
                              m_metadata.trimInSamples + (m_metadata.sampleRate / 75));
      }

      m_metadata.trimOutSamples = currentPos;
      updateTrimInfoLabel();

      if (m_waveformDisplay) {
        m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      }

      // Update preview player (no restart needed for OUT point)
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);

      DBG("ClipEditDialog: HOLD - Set OUT point to current position " << currentPos);
    }
  };
  addAndMakeVisible(m_trimOutHoldButton.get());

  // CLEAR button for OUT point (reset to max duration)
  m_trimOutClearButton = std::make_unique<juce::TextButton>("CLR");
  m_trimOutClearButton->onClick = [this]() {
    m_metadata.trimOutSamples = m_metadata.durationSamples;

    updateTrimInfoLabel();

    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    // Update preview player
    if (m_previewPlayer) {
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }

    DBG("ClipEditDialog: OUT point cleared to max duration (" << m_metadata.durationSamples
                                                              << " samples)");
  };
  addAndMakeVisible(m_trimOutClearButton.get());

  // Trim Info Label (shows duration in seconds)
  m_trimInfoLabel = std::make_unique<juce::Label>("trimInfoLabel", "Duration: --:--");
  m_trimInfoLabel->setFont(juce::Font("Inter", 12.0f, juce::Font::plain));
  m_trimInfoLabel->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(m_trimInfoLabel.get());

  // File Info Panel (SpotOn-style yellow background)
  m_fileInfoPanel = std::make_unique<juce::Label>("fileInfoPanel", "");
  m_fileInfoPanel->setFont(juce::Font("Inter", 11.0f, juce::Font::plain));
  m_fileInfoPanel->setJustificationType(juce::Justification::centredLeft);
  m_fileInfoPanel->setColour(juce::Label::backgroundColourId, juce::Colour(0xfffff4cc)); // Yellow
  m_fileInfoPanel->setColour(juce::Label::textColourId, juce::Colours::black);
  addAndMakeVisible(m_fileInfoPanel.get());
}

void ClipEditDialog::buildPhase3UI() {
  // Fade In Section
  m_fadeInLabel = std::make_unique<juce::Label>("fadeInLabel", "Fade In:");
  m_fadeInLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_fadeInLabel.get());

  m_fadeInCombo = std::make_unique<juce::ComboBox>();
  m_fadeInCombo->addItem("0.0 s", 1);
  m_fadeInCombo->addItem("0.1 s", 2);
  m_fadeInCombo->addItem("0.2 s", 3);
  m_fadeInCombo->addItem("0.3 s", 4);
  m_fadeInCombo->addItem("0.5 s", 5);
  m_fadeInCombo->addItem("1.0 s", 6);
  m_fadeInCombo->addItem("1.5 s", 7);
  m_fadeInCombo->addItem("2.0 s", 8);
  m_fadeInCombo->addItem("3.0 s", 9);
  m_fadeInCombo->setSelectedId(1, juce::dontSendNotification);
  m_fadeInCombo->onChange = [this]() {
    switch (m_fadeInCombo->getSelectedId()) {
    case 1:
      m_metadata.fadeInSeconds = 0.0;
      break;
    case 2:
      m_metadata.fadeInSeconds = 0.1;
      break;
    case 3:
      m_metadata.fadeInSeconds = 0.2;
      break;
    case 4:
      m_metadata.fadeInSeconds = 0.3;
      break;
    case 5:
      m_metadata.fadeInSeconds = 0.5;
      break;
    case 6:
      m_metadata.fadeInSeconds = 1.0;
      break;
    case 7:
      m_metadata.fadeInSeconds = 1.5;
      break;
    case 8:
      m_metadata.fadeInSeconds = 2.0;
      break;
    case 9:
      m_metadata.fadeInSeconds = 3.0;
      break;
    }
    // Update preview player fades
    if (m_previewPlayer) {
      m_previewPlayer->setFades(m_metadata.fadeInSeconds, m_metadata.fadeOutSeconds,
                                m_metadata.fadeInCurve, m_metadata.fadeOutCurve);
    }
  };
  addAndMakeVisible(m_fadeInCombo.get());

  m_fadeInCurveCombo = std::make_unique<juce::ComboBox>();
  m_fadeInCurveCombo->addItem("Linear", 1);
  m_fadeInCurveCombo->addItem("Equal Power", 2);
  m_fadeInCurveCombo->addItem("Exponential", 3);
  m_fadeInCurveCombo->setSelectedId(1, juce::dontSendNotification);
  m_fadeInCurveCombo->onChange = [this]() {
    switch (m_fadeInCurveCombo->getSelectedId()) {
    case 1:
      m_metadata.fadeInCurve = "Linear";
      break;
    case 2:
      m_metadata.fadeInCurve = "EqualPower";
      break;
    case 3:
      m_metadata.fadeInCurve = "Exponential";
      break;
    }
    // Update preview player fades
    if (m_previewPlayer) {
      m_previewPlayer->setFades(m_metadata.fadeInSeconds, m_metadata.fadeOutSeconds,
                                m_metadata.fadeInCurve, m_metadata.fadeOutCurve);
    }
  };
  addAndMakeVisible(m_fadeInCurveCombo.get());

  // Fade Out Section
  m_fadeOutLabel = std::make_unique<juce::Label>("fadeOutLabel", "Fade Out:");
  m_fadeOutLabel->setFont(juce::Font("Inter", 14.0f, juce::Font::bold));
  addAndMakeVisible(m_fadeOutLabel.get());

  m_fadeOutCombo = std::make_unique<juce::ComboBox>();
  m_fadeOutCombo->addItem("0.0 s", 1);
  m_fadeOutCombo->addItem("0.1 s", 2);
  m_fadeOutCombo->addItem("0.2 s", 3);
  m_fadeOutCombo->addItem("0.3 s", 4);
  m_fadeOutCombo->addItem("0.5 s", 5);
  m_fadeOutCombo->addItem("1.0 s", 6);
  m_fadeOutCombo->addItem("1.5 s", 7);
  m_fadeOutCombo->addItem("2.0 s", 8);
  m_fadeOutCombo->addItem("3.0 s", 9);
  m_fadeOutCombo->setSelectedId(1, juce::dontSendNotification);
  m_fadeOutCombo->onChange = [this]() {
    switch (m_fadeOutCombo->getSelectedId()) {
    case 1:
      m_metadata.fadeOutSeconds = 0.0;
      break;
    case 2:
      m_metadata.fadeOutSeconds = 0.1;
      break;
    case 3:
      m_metadata.fadeOutSeconds = 0.2;
      break;
    case 4:
      m_metadata.fadeOutSeconds = 0.3;
      break;
    case 5:
      m_metadata.fadeOutSeconds = 0.5;
      break;
    case 6:
      m_metadata.fadeOutSeconds = 1.0;
      break;
    case 7:
      m_metadata.fadeOutSeconds = 1.5;
      break;
    case 8:
      m_metadata.fadeOutSeconds = 2.0;
      break;
    case 9:
      m_metadata.fadeOutSeconds = 3.0;
      break;
    }
    // Update preview player fades
    if (m_previewPlayer) {
      m_previewPlayer->setFades(m_metadata.fadeInSeconds, m_metadata.fadeOutSeconds,
                                m_metadata.fadeInCurve, m_metadata.fadeOutCurve);
    }
  };
  addAndMakeVisible(m_fadeOutCombo.get());

  m_fadeOutCurveCombo = std::make_unique<juce::ComboBox>();
  m_fadeOutCurveCombo->addItem("Linear", 1);
  m_fadeOutCurveCombo->addItem("Equal Power", 2);
  m_fadeOutCurveCombo->addItem("Exponential", 3);
  m_fadeOutCurveCombo->setSelectedId(1, juce::dontSendNotification);
  m_fadeOutCurveCombo->onChange = [this]() {
    switch (m_fadeOutCurveCombo->getSelectedId()) {
    case 1:
      m_metadata.fadeOutCurve = "Linear";
      break;
    case 2:
      m_metadata.fadeOutCurve = "EqualPower";
      break;
    case 3:
      m_metadata.fadeOutCurve = "Exponential";
      break;
    }
    // Update preview player fades
    if (m_previewPlayer) {
      m_previewPlayer->setFades(m_metadata.fadeInSeconds, m_metadata.fadeOutSeconds,
                                m_metadata.fadeInCurve, m_metadata.fadeOutCurve);
    }
  };
  addAndMakeVisible(m_fadeOutCurveCombo.get());
}

//==============================================================================
void ClipEditDialog::paint(juce::Graphics& g) {
  // Dark background
  g.fillAll(juce::Colour(0xff1a1a1a));

  // Title bar
  g.setColour(juce::Colour(0xff252525));
  g.fillRect(0, 0, getWidth(), 50);

  // Title text
  g.setColour(juce::Colours::white);
  g.setFont(juce::Font("Inter", 20.0f, juce::Font::bold));
  g.drawText("Edit Clip", 20, 0, 400, 50, juce::Justification::centredLeft, false);
}

void ClipEditDialog::resized() {
  // SpotOn-inspired professional grid layout
  const int GRID = 10; // 10px grid unit
  auto bounds = getLocalBounds();

  // Title bar (50px)
  bounds.removeFromTop(50);

  // Content area with padding
  auto contentArea = bounds.reduced(GRID * 2);

  // File Info Panel at very top (yellow background, SpotOn-style)
  if (m_fileInfoPanel) {
    m_fileInfoPanel->setBounds(contentArea.removeFromTop(GRID * 3));
  }
  contentArea.removeFromTop(GRID); // Spacing

  // === WAVEFORM SECTION (Prominent, SpotOn-style) ===
  if (m_waveformDisplay) {
    m_waveformDisplay->setBounds(contentArea.removeFromTop(GRID * 15)); // Larger waveform
  }
  contentArea.removeFromTop(GRID);

  // === TRANSPORT BAR (Prominent, centered like SpotOn) ===
  if (m_playButton && m_stopButton && m_loopButton && m_transportPositionLabel) {
    auto transportRow = contentArea.removeFromTop(GRID * 4);
    auto transportCenter = transportRow.withSizeKeepingCentre(GRID * 40, GRID * 4);

    // Rewind button placeholder (left)
    auto rewindArea = transportCenter.removeFromLeft(GRID * 5);

    // Loop button
    m_loopButton->setBounds(transportCenter.removeFromLeft(GRID * 6));
    transportCenter.removeFromLeft(GRID);

    // Play button (larger, prominent)
    m_playButton->setBounds(transportCenter.removeFromLeft(GRID * 7));
    transportCenter.removeFromLeft(GRID);

    // Stop button
    m_stopButton->setBounds(transportCenter.removeFromLeft(GRID * 7));
    transportCenter.removeFromLeft(GRID);

    // Fast forward placeholder (right)
    auto ffArea = transportCenter.removeFromLeft(GRID * 5);

    // Transport position label (centered below)
    m_transportPositionLabel->setBounds(transportRow.withSizeKeepingCentre(GRID * 15, GRID * 3));
  }
  contentArea.removeFromTop(GRID);

  // === TRIM SECTION (Grid-based, SpotOn-style) ===
  auto trimSection = contentArea.removeFromTop(GRID * 12);

  // Trim IN (left column)
  auto trimInCol = trimSection.removeFromLeft(trimSection.getWidth() / 2 - GRID);
  if (m_trimInLabel && m_trimInTimeEditor && m_trimInDecButton && m_trimInIncButton &&
      m_trimInHoldButton && m_trimInClearButton) {
    m_trimInLabel->setBounds(trimInCol.removeFromTop(GRID * 2));
    trimInCol.removeFromTop(GRID / 2);

    auto trimInRow = trimInCol.removeFromTop(GRID * 3);
    m_trimInTimeEditor->setBounds(trimInRow.removeFromLeft(GRID * 10));
    trimInRow.removeFromLeft(GRID);
    m_trimInDecButton->setBounds(trimInRow.removeFromLeft(GRID * 3));
    m_trimInIncButton->setBounds(trimInRow.removeFromLeft(GRID * 3));

    trimInCol.removeFromTop(GRID / 2);
    auto trimInButtonRow = trimInCol.removeFromTop(GRID * 3);
    m_trimInClearButton->setBounds(trimInButtonRow.removeFromLeft(GRID * 5));
    trimInButtonRow.removeFromLeft(GRID);
    m_trimInHoldButton->setBounds(trimInButtonRow.removeFromLeft(GRID * 5));
  }

  trimSection.removeFromLeft(GRID * 2); // Column spacing

  // Trim OUT (right column)
  auto trimOutCol = trimSection;
  if (m_trimOutLabel && m_trimOutTimeEditor && m_trimOutDecButton && m_trimOutIncButton &&
      m_trimOutHoldButton && m_trimOutClearButton) {
    m_trimOutLabel->setBounds(trimOutCol.removeFromTop(GRID * 2));
    trimOutCol.removeFromTop(GRID / 2);

    auto trimOutRow = trimOutCol.removeFromTop(GRID * 3);
    m_trimOutTimeEditor->setBounds(trimOutRow.removeFromLeft(GRID * 10));
    trimOutRow.removeFromLeft(GRID);
    m_trimOutDecButton->setBounds(trimOutRow.removeFromLeft(GRID * 3));
    m_trimOutIncButton->setBounds(trimOutRow.removeFromLeft(GRID * 3));

    trimOutCol.removeFromTop(GRID / 2);
    auto trimOutButtonRow = trimOutCol.removeFromTop(GRID * 3);
    m_trimOutClearButton->setBounds(trimOutButtonRow.removeFromLeft(GRID * 5));
    trimOutButtonRow.removeFromLeft(GRID);
    m_trimOutHoldButton->setBounds(trimOutButtonRow.removeFromLeft(GRID * 5));
  }

  contentArea.removeFromTop(GRID);

  // Duration label (centered)
  if (m_trimInfoLabel) {
    m_trimInfoLabel->setBounds(contentArea.removeFromTop(GRID * 2));
  }
  contentArea.removeFromTop(GRID * 2);

  // === METADATA SECTION ===
  // Clip Name
  auto nameRow = contentArea.removeFromTop(GRID * 6);
  m_nameLabel->setBounds(nameRow.removeFromTop(GRID * 2));
  m_nameEditor->setBounds(nameRow.removeFromTop(GRID * 3));
  contentArea.removeFromTop(GRID);

  // Color and Group (inline)
  auto metadataRow = contentArea.removeFromTop(GRID * 3);
  m_colorLabel->setBounds(metadataRow.removeFromLeft(GRID * 6));
  m_colorComboBox->setBounds(metadataRow.removeFromLeft(GRID * 12));
  metadataRow.removeFromLeft(GRID * 2);
  m_groupLabel->setBounds(metadataRow.removeFromLeft(GRID * 9));
  m_groupComboBox->setBounds(metadataRow.removeFromLeft(GRID * 13));
  contentArea.removeFromTop(GRID * 2);

  // === FADE SECTION ===
  auto fadeSection = contentArea.removeFromTop(GRID * 8);

  // Fade IN (left column)
  auto fadeInCol = fadeSection.removeFromLeft(fadeSection.getWidth() / 2 - GRID);
  if (m_fadeInLabel && m_fadeInCombo && m_fadeInCurveCombo) {
    m_fadeInLabel->setBounds(fadeInCol.removeFromTop(GRID * 2));
    fadeInCol.removeFromTop(GRID / 2);
    auto fadeInRow = fadeInCol.removeFromTop(GRID * 3);
    m_fadeInCombo->setBounds(fadeInRow.removeFromLeft(GRID * 10));
    fadeInRow.removeFromLeft(GRID);
    m_fadeInCurveCombo->setBounds(fadeInRow);
  }

  fadeSection.removeFromLeft(GRID * 2);

  // Fade OUT (right column)
  auto fadeOutCol = fadeSection;
  if (m_fadeOutLabel && m_fadeOutCombo && m_fadeOutCurveCombo) {
    m_fadeOutLabel->setBounds(fadeOutCol.removeFromTop(GRID * 2));
    fadeOutCol.removeFromTop(GRID / 2);
    auto fadeOutRow = fadeOutCol.removeFromTop(GRID * 3);
    m_fadeOutCombo->setBounds(fadeOutRow.removeFromLeft(GRID * 10));
    fadeOutRow.removeFromLeft(GRID);
    m_fadeOutCurveCombo->setBounds(fadeOutRow);
  }

  // Dialog buttons at bottom
  auto buttonArea = contentArea.removeFromBottom(GRID * 4);
  m_cancelButton->setBounds(buttonArea.removeFromRight(GRID * 10));
  buttonArea.removeFromRight(GRID);
  m_okButton->setBounds(buttonArea.removeFromRight(GRID * 10));
}

//==============================================================================
bool ClipEditDialog::keyPressed(const juce::KeyPress& key) {
  // Keyboard shortcuts (SpotOn-inspired accessibility)
  // I = Set IN point (at current transport position, for now use middle of visible region)
  // O = Set OUT point (at current transport position, for now use middle of visible region)
  // J = Jump transport (at current transport position, not yet implemented)
  // [ = Nudge IN point left (-1 tick)
  // ] = Nudge IN point right (+1 tick)
  // { = Nudge OUT point left (-1 tick)
  // } = Nudge OUT point right (+1 tick)

  int64_t tickInSamples = m_metadata.sampleRate / 75;

  // I key: Set IN point to current preview playback position
  if (key == juce::KeyPress('i') || key == juce::KeyPress('I')) {
    if (m_previewPlayer) {
      int64_t currentPos = m_previewPlayer->getCurrentPosition();
      m_metadata.trimInSamples =
          std::clamp(currentPos, int64_t(0), m_metadata.trimOutSamples - tickInSamples);
      updateTrimInfoLabel();
      if (m_waveformDisplay) {
        m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      }
      // Update preview player and restart from new IN point
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      m_previewPlayer->play();
      DBG("ClipEditDialog: 'I' key - Set IN point to sample " << m_metadata.trimInSamples);
    }
    return true;
  }

  // O key: Set OUT point to current preview playback position
  if (key == juce::KeyPress('o') || key == juce::KeyPress('O')) {
    if (m_previewPlayer) {
      int64_t currentPos = m_previewPlayer->getCurrentPosition();
      m_metadata.trimOutSamples = std::clamp(currentPos, m_metadata.trimInSamples + tickInSamples,
                                             m_metadata.durationSamples);
      updateTrimInfoLabel();
      if (m_waveformDisplay) {
        m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      }
      // Update preview player (no restart needed for OUT point)
      m_previewPlayer->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
      DBG("ClipEditDialog: 'O' key - Set OUT point to sample " << m_metadata.trimOutSamples);
    }
    return true;
  }

  // J key: Jump transport (not yet implemented)
  if (key == juce::KeyPress('j') || key == juce::KeyPress('J')) {
    DBG("ClipEditDialog: 'J' key - Transport jump (not yet implemented)");
    return true;
  }

  // [ key: Nudge IN point left (-1 tick)
  if (key == juce::KeyPress('[')) {
    m_metadata.trimInSamples = std::max(int64_t(0), m_metadata.trimInSamples - tickInSamples);
    updateTrimInfoLabel();
    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
    DBG("ClipEditDialog: '[' key - Nudged IN point left to sample " << m_metadata.trimInSamples);
    return true;
  }

  // ] key: Nudge IN point right (+1 tick)
  if (key == juce::KeyPress(']')) {
    m_metadata.trimInSamples =
        std::min(m_metadata.trimOutSamples, m_metadata.trimInSamples + tickInSamples);
    updateTrimInfoLabel();
    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
    DBG("ClipEditDialog: ']' key - Nudged IN point right to sample " << m_metadata.trimInSamples);
    return true;
  }

  // { key (Shift+[): Nudge OUT point left (-1 tick)
  if (key == juce::KeyPress('{')) {
    m_metadata.trimOutSamples =
        std::max(m_metadata.trimInSamples, m_metadata.trimOutSamples - tickInSamples);
    updateTrimInfoLabel();
    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
    DBG("ClipEditDialog: '{' key - Nudged OUT point left to sample " << m_metadata.trimOutSamples);
    return true;
  }

  // } key (Shift+]): Nudge OUT point right (+1 tick)
  if (key == juce::KeyPress('}')) {
    m_metadata.trimOutSamples =
        std::min(m_metadata.durationSamples, m_metadata.trimOutSamples + tickInSamples);
    updateTrimInfoLabel();
    if (m_waveformDisplay) {
      m_waveformDisplay->setTrimPoints(m_metadata.trimInSamples, m_metadata.trimOutSamples);
    }
    DBG("ClipEditDialog: '}' key - Nudged OUT point right to sample " << m_metadata.trimOutSamples);
    return true;
  }

  return Component::keyPressed(key);
}
