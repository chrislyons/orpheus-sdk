// SPDX-License-Identifier: MIT

#include "ClipButton.h"
#include "ClipGrid.h"

//==============================================================================
ClipButton::ClipButton(int buttonIndex) : m_buttonIndex(buttonIndex) {
  // Default empty state
  m_state = State::Empty;
  m_clipColor = juce::Colours::darkgrey;
  // m_clipName default-constructs to empty string, no need to assign
}

//==============================================================================
void ClipButton::setState(State newState) {
  if (m_state != newState) {
    m_state = newState;
    repaint();
  }
}

void ClipButton::setClipName(const juce::String& name) {
  m_clipName = name;
  repaint();
}

void ClipButton::setClipColor(juce::Colour color) {
  m_clipColor = color;
  repaint();
}

void ClipButton::setClipDuration(double durationSeconds) {
  m_durationSeconds = durationSeconds;
  repaint();
}

void ClipButton::setClipGroup(int group) {
  m_clipGroup = juce::jlimit(0, 3, group);
  repaint();
}

void ClipButton::setKeyboardShortcut(const juce::String& shortcut) {
  m_keyboardShortcut = shortcut;
  repaint();
}

void ClipButton::setBeatOffset(const juce::String& beatOffset) {
  m_beatOffset = beatOffset;
  repaint();
}

void ClipButton::clearClip() {
  m_state = State::Empty;
  m_clipName.clear();
  m_clipColor = juce::Colours::darkgrey;
  m_durationSeconds = 0.0;
  m_clipGroup = 0;
  m_keyboardShortcut.clear();
  m_beatOffset.clear();
  m_playbackProgress = 0.0f;
  m_loopEnabled = false;
  m_fadeInEnabled = false;
  m_fadeOutEnabled = false;
  m_effectsEnabled = false;
  m_stopOthersEnabled = false;
  repaint();
}

void ClipButton::setPlaybackProgress(float progress) {
  m_playbackProgress = juce::jlimit(0.0f, 1.0f, progress);

  // Only repaint if playing (avoid unnecessary repaints)
  if (m_state == State::Playing || m_state == State::Stopping)
    repaint();
}

void ClipButton::setLoopEnabled(bool enabled) {
  m_loopEnabled = enabled;
  repaint();
}

void ClipButton::setFadeInEnabled(bool enabled) {
  m_fadeInEnabled = enabled;
  repaint();
}

void ClipButton::setFadeOutEnabled(bool enabled) {
  m_fadeOutEnabled = enabled;
  repaint();
}

void ClipButton::setEffectsEnabled(bool enabled) {
  m_effectsEnabled = enabled;
  repaint();
}

void ClipButton::setStopOthersEnabled(bool enabled) {
  m_stopOthersEnabled = enabled;
  repaint();
}

//==============================================================================
juce::String ClipButton::formatDuration(double seconds) const {
  int totalSeconds = static_cast<int>(seconds);
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int secs = totalSeconds % 60;

  // Hide HH: if duration < 60 minutes
  // Never show .FF field (too busy/CPU intensive)
  if (hours > 0) {
    return juce::String::formatted("%d:%02d:%02d", hours, minutes, secs);
  } else {
    return juce::String::formatted("%d:%02d", minutes, secs);
  }
}

void ClipButton::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds().toFloat();

  // Background color based on state
  juce::Colour bgColor;
  juce::Colour borderColor;

  switch (m_state) {
  case State::Empty:
    bgColor = juce::Colour(0xff2a2a2a);     // Dark grey
    borderColor = juce::Colour(0xff404040); // Slightly lighter border
    break;

  case State::Loaded:
    bgColor = m_clipColor.withAlpha(0.9f); // 90% opacity clip color
    borderColor = m_clipColor.darker(0.2f);
    break;

  case State::Playing:
    // Glowing pulsing border instead of bright fill (preserve clip color, only animate border)
    bgColor = m_clipColor.withAlpha(0.9f); // Keep 90% opacity clip color
    borderColor = juce::Colours::white;    // White glowing border
    break;

  case State::Stopping:
    bgColor = m_clipColor.withAlpha(0.9f); // 90% opacity
    borderColor = juce::Colours::orange;   // Orange border during fade-out
    break;
  }

  // Draw button background with rounded corners
  g.setColour(bgColor);
  g.fillRoundedRectangle(bounds.reduced(1.0f), CORNER_RADIUS);

  // Draw border (animated for Playing state)
  if (m_state == State::Playing) {
    // Glowing pulsing border for playing state
    // Use timestamp for pulsing animation
    auto now = juce::Time::getMillisecondCounterHiRes();
    float pulsePhase = std::fmod(now / 500.0, 1.0); // Fast pulse: 500ms cycle
    float pulseAlpha = 0.6f + 0.4f * std::sin(pulsePhase * juce::MathConstants<float>::twoPi);

    // Draw thick glowing border
    g.setColour(borderColor.withAlpha(pulseAlpha));
    g.drawRoundedRectangle(bounds.reduced(1.0f), CORNER_RADIUS,
                           5.0f); // Thick border for prominent glow

    // Trigger repaint for animation (only when playing)
    repaint();
  } else {
    // Normal border for other states
    g.setColour(borderColor);
    g.drawRoundedRectangle(bounds.reduced(1.0f), CORNER_RADIUS, BORDER_THICKNESS);
  }

  if (m_state == State::Empty) {
    // Button index (larger, more prominent) - 20% increase: 18 -> 21.6
    // Feature 4: Use consecutive numbering across tabs
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.setFont(juce::FontOptions("Inter", 21.6f, juce::Font::bold));
    g.drawText(juce::String(getDisplayNumber()), bounds, juce::Justification::centred, false);

    // No "Empty" text - just the button number on grey background is sufficient
  } else {
    // Modern HUD layout for loaded clips
    drawClipHUD(g, bounds);
  }
}

void ClipButton::drawClipHUD(juce::Graphics& g, juce::Rectangle<float> bounds) {
  auto contentArea = bounds.reduced(PADDING);
  float currentY = contentArea.getY();

  // Determine text color based on background brightness
  // Skew towards white/light text (dark mode app) - only use black on VERY light backgrounds
  juce::Colour bgColor;
  switch (m_state) {
  case State::Loaded:
    bgColor = m_clipColor.withAlpha(0.9f);
    break;
  case State::Playing:
    bgColor = m_clipColor.withAlpha(0.9f);
    break;
  case State::Stopping:
    bgColor = m_clipColor.withAlpha(0.9f);
    break;
  default:
    bgColor = juce::Colours::darkgrey;
    break;
  }

  // Use black text ONLY on extremely light backgrounds (>0.8), white otherwise
  // This ensures readability while strongly favoring white text for dark mode aesthetic
  float brightness = bgColor.getBrightness();
  juce::Colour textColor =
      brightness > 0.8f ? juce::Colours::black.withAlpha(0.95f) : juce::Colours::white;
  juce::Colour subtleTextColor = textColor.withAlpha(0.6f);
  juce::Colour prominentTextColor = textColor.withAlpha(0.95f);

  // === TOP ROW: Button Index (white rounded box) + Keyboard Shortcut ===
  {
    auto topRow = contentArea.removeFromTop(16.0f);

    // Button index in white rounded rectangle (Feature 3)
    // Feature 4: Use consecutive numbering across tabs
    juce::String buttonNumber = juce::String(getDisplayNumber());
    auto numberFont = juce::FontOptions("Inter", 12.0f, juce::Font::bold);
    g.setFont(numberFont);

    // Calculate text width using GlyphArrangement (replaces deprecated getStringWidth)
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(numberFont), buttonNumber, 0.0f, 0.0f);
    float textWidth = glyphs.getBoundingBox(0, -1, true).getWidth();
    float boxWidth = textWidth + 8.0f; // Padding
    float boxHeight = 16.0f;

    auto numberBox = topRow.removeFromLeft(boxWidth).withHeight(boxHeight);

    // Draw white rounded rectangle background
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.fillRoundedRectangle(numberBox, 3.0f);

    // Draw black text
    g.setColour(juce::Colours::black);
    g.drawText(buttonNumber, numberBox, juce::Justification::centred, false);

    // Keyboard shortcut (right, prominent) - 20% increase: 11 -> 13.2
    if (m_keyboardShortcut.isNotEmpty()) {
      g.setColour(prominentTextColor);
      g.setFont(juce::FontOptions("Inter", 13.2f, juce::Font::bold));
      g.drawText(m_keyboardShortcut, topRow, juce::Justification::topRight, false);
    }

    currentY = topRow.getBottom() + 2.0f;
  }

  // === MIDDLE: Clip Name (PRIMARY) + Duration (secondary) ===
  {
    // Give most space to clip name
    float nameHeight = contentArea.getHeight() * 0.65f;
    auto nameArea =
        juce::Rectangle<float>(contentArea.getX(), currentY, contentArea.getWidth(), nameHeight);

    // Clip Name (PRIMARY - MUCH larger, bold, 3 lines)
    g.setColour(textColor);
    g.setFont(juce::FontOptions("Inter", 18.0f, juce::Font::bold));

    // Reserve minimal space for duration
    auto nameOnlyArea = nameArea.withTrimmedBottom(12.0f);
    g.drawFittedText(m_clipName, nameOnlyArea.toNearestInt(), juce::Justification::centred,
                     3, // Allow up to 3 lines for name
                     0.85f);

    // Duration (secondary - MUCH smaller and subtle)
    auto durationArea = nameArea.removeFromBottom(11.0f);
    if (m_durationSeconds > 0.0) {
      g.setFont(juce::FontOptions("Inter", 9.0f, juce::Font::plain));

      if (m_state == State::Playing && m_playbackProgress > 0.0f) {
        // Show elapsed / remaining during playback
        double elapsed = m_durationSeconds * m_playbackProgress;
        double remaining = m_durationSeconds - elapsed;
        juce::String timeDisplay =
            "▶ " + formatDuration(elapsed) + " / -" + formatDuration(remaining);
        g.setColour(juce::Colour(0xff00ff00).withAlpha(0.8f));
        g.drawText(timeDisplay, durationArea, juce::Justification::centred, false);
      } else {
        // Show total duration when stopped (use adaptive subtle color)
        g.setColour(subtleTextColor);
        g.drawText(formatDuration(m_durationSeconds), durationArea, juce::Justification::centred,
                   false);
      }
    }

    currentY = nameArea.getBottom();
  }

  // === BOTTOM ROW: Beat Offset + Group ===
  {
    auto bottomArea = juce::Rectangle<float>(contentArea.getX(), contentArea.getBottom() - 24.0f,
                                             contentArea.getWidth(), 24.0f);

    // Beat offset (left, if present) - e.g., "//3+"
    if (m_beatOffset.isNotEmpty()) {
      g.setColour(juce::Colour(0xffffaa00)); // Orange for timing info
      g.setFont(juce::FontOptions("Inter", 12.0f, juce::Font::bold));
      g.drawText("//" + m_beatOffset, bottomArea.withTrimmedRight(bottomArea.getWidth() * 0.7f),
                 juce::Justification::centredLeft, false);
    }

    // Clip group indicator (right) - e.g., "G1", "G2"
    {
      juce::Colour groupColors[4] = {
          juce::Colour(0xff3498db), // Blue - Group 0
          juce::Colour(0xff2ecc71), // Green - Group 1
          juce::Colour(0xfff39c12), // Orange - Group 2
          juce::Colour(0xffe74c3c)  // Red - Group 3
      };

      auto groupBadge = bottomArea.removeFromRight(24.0f).withTrimmedTop(4.0f).withHeight(16.0f);

      // Draw group badge background
      g.setColour(groupColors[m_clipGroup].withAlpha(0.8f));
      g.fillRoundedRectangle(groupBadge, 3.0f);

      // Draw group number
      g.setColour(juce::Colours::white);
      g.setFont(juce::FontOptions("Inter", 10.8f, juce::Font::bold));
      g.drawText("G" + juce::String(m_clipGroup + 1), groupBadge, juce::Justification::centred,
                 false);
    }
  }

  // === PLAYING STATE INDICATOR ===
  if (m_state == State::Playing) {
    // Play triangle icon (top-right corner, prominent) - 20% increase: 20 -> 24, 10 -> 12
    auto playIconBounds = bounds.removeFromTop(24.0f).removeFromRight(24.0f).reduced(4.0f);

    // Draw play triangle (pointing right)
    juce::Path playTriangle;
    float cx = playIconBounds.getCentreX();
    float cy = playIconBounds.getCentreY();
    float size = 12.0f; // 20% increase: 10.0 -> 12.0

    playTriangle.addTriangle(cx - size * 0.3f, cy - size * 0.5f, // Top-left
                             cx - size * 0.3f, cy + size * 0.5f, // Bottom-left
                             cx + size * 0.6f, cy                // Right point
    );

    // Bright green background circle
    g.setColour(juce::Colour(0xff00ff00)); // Bright green
    g.fillEllipse(playIconBounds);

    // White play triangle
    g.setColour(juce::Colours::white);
    g.fillPath(playTriangle);
  }

  // === PROGRESS BAR ===
  if ((m_state == State::Playing || m_state == State::Stopping) && m_playbackProgress > 0.0f) {
    // Draw progress bar at the very bottom of the button
    auto progressArea = bounds.removeFromBottom(3.0f).reduced(1.0f, 0.0f);
    float progressWidth = progressArea.getWidth() * m_playbackProgress;

    // Background (darker)
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(progressArea, 1.5f);

    // Progress fill (bright accent color)
    if (progressWidth > 0.0f) {
      auto fillArea = progressArea.withWidth(progressWidth);
      g.setColour(m_state == State::Playing ? juce::Colours::cyan : juce::Colours::orange);
      g.fillRoundedRectangle(fillArea, 1.5f);
    }
  }

  // === STATUS INDICATORS ===
  // Draw status indicators in bottom-left corner (Feature 2)
  // Order: PLAY | LOOP | STOP OTHERS | FADE IN | FADE OUT | SPEED (%)
  // Only show when state is TRUE, use fixed positions (blank space if false)
  if (m_state == State::Playing || m_loopEnabled || m_stopOthersEnabled || m_fadeInEnabled ||
      m_fadeOutEnabled) {
    auto indicatorArea = juce::Rectangle<float>(contentArea.getX(), contentArea.getBottom() - 16.0f,
                                                contentArea.getWidth(), 14.0f);
    drawStatusIcons(g, indicatorArea);
  }
}

void ClipButton::drawStatusIcons(juce::Graphics& g, juce::Rectangle<float> bounds) {
  // Feature 2: Text-based status indicators in bottom-left corner
  // Order: PLAY | LOOP | STOP OTHERS | FADE IN | FADE OUT | SPEED (%)
  // Only display when state is TRUE (blank space if false)

  juce::String indicators;
  juce::Colour indicatorColor = juce::Colours::white.withAlpha(0.9f);

  // Build indicator string with separators (only show TRUE states)
  bool hasAny = false;

  // 1. PLAY indicator (only when playing)
  if (m_state == State::Playing) {
    indicators += "PLAY";
    hasAny = true;
  }

  // 2. LOOP indicator
  if (m_loopEnabled) {
    if (hasAny)
      indicators += " | ";
    indicators += "LOOP";
    hasAny = true;
  }

  // 3. STOP OTHERS indicator
  if (m_stopOthersEnabled) {
    if (hasAny)
      indicators += " | ";
    indicators += "STOP OTHERS";
    hasAny = true;
  }

  // 4. FADE IN indicator
  if (m_fadeInEnabled) {
    if (hasAny)
      indicators += " | ";
    indicators += "FADE IN";
    hasAny = true;
  }

  // 5. FADE OUT indicator
  if (m_fadeOutEnabled) {
    if (hasAny)
      indicators += " | ";
    indicators += "FADE OUT";
    hasAny = true;
  }

  // 6. SPEED indicator (placeholder for future implementation)
  // if (speedModifier != 100) {
  //   if (hasAny) indicators += " | ";
  //   indicators += "SPEED (" + String(speedModifier) + "%)";
  //   hasAny = true;
  // }

  // Draw text indicators
  if (hasAny) {
    g.setColour(indicatorColor);
    g.setFont(juce::FontOptions("Inter", 8.0f, juce::Font::plain));
    g.drawText(indicators, bounds, juce::Justification::centredLeft, false);
  }
}

void ClipButton::resized() {
  // No child components yet, layout handled in paint()
}

void ClipButton::mouseDown(const juce::MouseEvent& e) {
  if (e.mods.isLeftButtonDown()) {
    // Check for Ctrl+Opt+Cmd+LeftClick to open right-click menu (Feature 1)
    // This provides an alternative to right-click for opening the context menu
    if (e.mods.isCommandDown() && e.mods.isCtrlDown() && e.mods.isAltDown()) {
      // Trigger right-click menu (works on both empty and loaded buttons)
      if (onRightClick)
        onRightClick(m_buttonIndex);
      return; // Don't process as drag or regular click
    }

    // Record mouse down position for potential Cmd+Drag rearrangement
    m_mouseDownPosition = e.getPosition();
    m_isDragging = false;

    // Fire click immediately (don't wait for mouseUp to avoid double-click delay)
    // This makes rapid clicking feel responsive
    if (!e.mods.isCommandDown() && m_state != State::Empty) {
      // Only fire if not holding Cmd (which would be drag-to-rearrange)
      if (onClick)
        onClick(m_buttonIndex);
    }
  } else if (e.mods.isRightButtonDown()) {
    // Right click - context menu (works on both empty and loaded buttons)
    if (onRightClick)
      onRightClick(m_buttonIndex);
  }
}

void ClipButton::mouseDrag(const juce::MouseEvent& e) {
  // Only allow drag if Cmd/Ctrl key is held and clip is loaded
  if (!e.mods.isLeftButtonDown() || !e.mods.isCommandDown() || m_state == State::Empty)
    return;

  // Check if we've moved enough to consider it a drag
  auto dragDistance = e.getPosition().getDistanceFrom(m_mouseDownPosition);
  if (dragDistance < 10.0f && !m_isDragging)
    return;

  m_isDragging = true;

  // Visual feedback: make button slightly transparent while dragging
  setAlpha(0.6f);
}

void ClipButton::mouseUp(const juce::MouseEvent& e) {
  // Restore full opacity
  setAlpha(1.0f);

  if (!e.mods.isLeftButtonDown())
    return;

  if (m_isDragging) {
    // We were dragging - find target button under mouse
    auto* grid = findParentComponentOfClass<ClipGrid>();
    if (grid) {
      // Convert to grid coordinates
      auto posInGrid = grid->getLocalPoint(this, e.getPosition());

      // Find which button we're over
      for (int i = 0; i < grid->getButtonCount(); ++i) {
        auto* targetButton = grid->getButton(i);
        if (targetButton && targetButton != this && targetButton->getBounds().contains(posInGrid)) {
          // Trigger drag callback
          if (onDragToButton) {
            onDragToButton(m_buttonIndex, i);
          }
          break;
        }
      }
    }
    m_isDragging = false;
  }
  // Note: Double-click behavior intentionally removed
  // Clip buttons prioritize single-click for PLAY/STOP at all times
  // Use right-click menu or Ctrl+Opt+Cmd+Click to access Edit Dialog
}
