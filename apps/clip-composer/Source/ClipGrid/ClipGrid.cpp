// SPDX-License-Identifier: MIT

#include "ClipGrid.h"

//==============================================================================
ClipGrid::ClipGrid() {
  createButtons();

  // Start 75fps timer for visual updates (broadcast standard timing)
  // 75fps = 13.33ms per frame (1000ms / 75 = 13.33ms)
  startTimer(13); // ~75fps (13ms is close enough to 13.33ms)
}

//==============================================================================
void ClipGrid::createButtons() {
  m_buttons.clear();

  // Create 48 buttons (6×8)
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    auto button = std::make_unique<ClipButton>(i);

    // Wire up callbacks
    button->onClick = [this](int index) { handleButtonLeftClick(index); };
    button->onRightClick = [this](int index) { handleButtonRightClick(index); };
    button->onDoubleClick = [this](int index) {
      if (onButtonDoubleClicked)
        onButtonDoubleClicked(index);
    };
    button->onDragToButton = [this](int sourceIndex, int targetIndex) {
      if (onButtonDraggedToButton) {
        onButtonDraggedToButton(sourceIndex, targetIndex);
      }
    };

    // All buttons start empty - clips will be loaded by SessionManager
    addAndMakeVisible(button.get());
    m_buttons.push_back(std::move(button));
  }
}

ClipButton* ClipGrid::getButton(int index) {
  if (index >= 0 && index < BUTTON_COUNT)
    return m_buttons[static_cast<size_t>(index)].get();
  return nullptr;
}

//==============================================================================
void ClipGrid::handleButtonLeftClick(int buttonIndex) {
  DBG("ClipGrid: Button " + juce::String(buttonIndex) + " left-clicked");

  // Forward to MainComponent via callback
  if (onButtonClicked)
    onButtonClicked(buttonIndex);
}

void ClipGrid::handleButtonRightClick(int buttonIndex) {
  DBG("ClipGrid: Button " + juce::String(buttonIndex) + " right-clicked");

  // Forward to MainComponent via callback
  if (onButtonRightClicked)
    onButtonRightClicked(buttonIndex);
}

//==============================================================================
void ClipGrid::paint(juce::Graphics& g) {
  // Grid background
  g.fillAll(juce::Colour(0xff1a1a1a)); // Very dark grey
}

void ClipGrid::resized() {
  auto bounds = getLocalBounds();

  // Calculate button size based on grid dimensions
  int availableWidth = bounds.getWidth() - (GAP * (COLUMNS + 1));
  int availableHeight = bounds.getHeight() - (GAP * (ROWS + 1));

  int buttonWidth = availableWidth / COLUMNS;
  int buttonHeight = availableHeight / ROWS;

  // Layout buttons in grid
  for (int row = 0; row < ROWS; ++row) {
    for (int col = 0; col < COLUMNS; ++col) {
      int index = row * COLUMNS + col;
      auto button = getButton(index);

      if (button) {
        int x = GAP + col * (buttonWidth + GAP);
        int y = GAP + row * (buttonHeight + GAP);

        button->setBounds(x, y, buttonWidth, buttonHeight);
      }
    }
  }
}

//==============================================================================
bool ClipGrid::isInterestedInFileDrag(const juce::StringArray& files) {
  // Accept any audio files
  for (const auto& file : files) {
    if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".aiff") ||
        file.endsWithIgnoreCase(".aif") || file.endsWithIgnoreCase(".flac")) {
      return true;
    }
  }
  return false;
}

void ClipGrid::filesDropped(const juce::StringArray& files, int x, int y) {
  // Find which button was dropped on
  int targetButtonIndex = -1;

  for (int i = 0; i < BUTTON_COUNT; ++i) {
    auto button = getButton(i);
    if (button && button->getBounds().contains(x, y)) {
      targetButtonIndex = i;
      break;
    }
  }

  // If dropped on a button, load files starting from that button
  // Otherwise, load starting from first empty button
  if (targetButtonIndex < 0) {
    targetButtonIndex = 0; // Default to first button
  }

  // Convert StringArray to Array<File>
  juce::Array<juce::File> audioFiles;
  for (const auto& filePath : files) {
    juce::File file(filePath);
    if (file.existsAsFile()) {
      audioFiles.add(file);
    }
  }

  if (audioFiles.isEmpty()) {
    DBG("ClipGrid: No valid audio files dropped");
    return;
  }

  DBG("ClipGrid: " << audioFiles.size() << " file(s) dropped on button " << targetButtonIndex);

  // Forward to MainComponent via callback
  if (onFilesDropped) {
    onFilesDropped(audioFiles, targetButtonIndex);
  }
}

//==============================================================================
void ClipGrid::timerCallback() {
  // Sync button states from AudioEngine at 75fps (broadcast standard timing)
  // This ensures visual indicators (play state, loop, fade, etc.) chase at 75fps
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    auto button = getButton(i);
    if (!button)
      continue;

    // Query AudioEngine for playback state (if callback is set)
    if (isClipPlaying) {
      bool playing = isClipPlaying(i);
      auto currentState = button->getState();

      // Update button state if it doesn't match AudioEngine state
      if (playing && currentState != ClipButton::State::Playing) {
        button->setState(ClipButton::State::Playing);
      } else if (!playing && currentState == ClipButton::State::Playing) {
        // Clip stopped (fade complete) - reset to Loaded
        if (currentState != ClipButton::State::Empty) {
          button->setState(ClipButton::State::Loaded);
        }
      }
    }

    // Repaint button to update visual indicators
    button->repaint();
  }
}
