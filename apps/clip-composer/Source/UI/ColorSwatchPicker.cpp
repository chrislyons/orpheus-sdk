// SPDX-License-Identifier: MIT

#include "ColorSwatchPicker.h"

//==============================================================================
// ColorSwatchGrid implementation (popup grid)
//==============================================================================
ColorSwatchGrid::ColorSwatchGrid() {
  initializeColorPalette();
  m_selectedIndex = 0;
  // 4:3 aspect swatches + 4px spacing, 4 rows × 12 columns = 48 swatches
  // Calculate optimal dimensions for tight fit:
  // Height = 80px, padding = 6px × 2 = 12px
  // Available height = 80 - 12 = 68px
  // Swatch height = (68 - (3 × 4spacing)) / 4 rows = (68 - 12) / 4 = 14px
  // Swatch width = 14 × 4/3 = 18.67 ≈ 19px (rounded up)
  // Total width = 12swatches × 19px + 11spacing × 4px + 12padding = 228 + 44 + 12 = 284px
  setSize(284, 80); // Tight border for 4×12 grid
}

void ColorSwatchGrid::initializeColorPalette() {
  // Custom color palette (4 rows × 12 columns = 48 swatches)
  // Arranged as smooth spectrum gradient: Reds → Oranges → Yellows → Greens → Cyans → Blues →
  // Purples → Pinks → Grays
  m_colorPalette = {// Row 1: Reds → Oranges → Yellows
                    juce::Colour(0xff822636), juce::Colour(0xffA42D32), juce::Colour(0xffE24040),
                    juce::Colour(0xffF56A5C), juce::Colour(0xffC85C3C), juce::Colour(0xffF59673),
                    juce::Colour(0xffF78A3E), juce::Colour(0xffE8A456), juce::Colour(0xffDBB949),
                    juce::Colour(0xffF5D94F), juce::Colour(0xffF5EAD6), juce::Colour(0xffCCAB8E),
                    // Row 2: Greens → Cyans
                    juce::Colour(0xff9FBE60), juce::Colour(0xffA4D68F), juce::Colour(0xffC4D2AE),
                    juce::Colour(0xff30BE56), juce::Colour(0xff6FC5A6), juce::Colour(0xffA9DBCE),
                    juce::Colour(0xff2B7F73), juce::Colour(0xff3E6F7C), juce::Colour(0xff6A92A2),
                    juce::Colour(0xff5EA9D9), juce::Colour(0xff26A3E1), juce::Colour(0xff2B7CBA),
                    // Row 3: Blues → Purples
                    juce::Colour(0xff8FC4E6), juce::Colour(0xff326FCF), juce::Colour(0xff2542A0),
                    juce::Colour(0xff21397A), juce::Colour(0xff12304E), juce::Colour(0xff7F7ACD),
                    juce::Colour(0xff9876D4), juce::Colour(0xffAF78E3), juce::Colour(0xff6848AD),
                    juce::Colour(0xffCEC8E4), juce::Colour(0xffBDAFE6), juce::Colour(0xffC6A6D6),
                    // Row 4: Pinks → Grays (light to dark)
                    juce::Colour(0xffE8B3C2), juce::Colour(0xffF5A9B8), juce::Colour(0xffE4879D),
                    juce::Colour(0xffD98092), juce::Colour(0xffC36F8C), juce::Colour(0xffD3A8BF),
                    juce::Colour(0xffF6F5F9), juce::Colour(0xffEDEAEE), juce::Colour(0xffD6D5DB),
                    juce::Colour(0xffACADB0), juce::Colour(0xff7D7F83), juce::Colour(0xff333A40)};
}

void ColorSwatchGrid::setSelectedColor(const juce::Colour& color) {
  // Find closest matching color in palette
  m_selectedIndex = -1;
  float minDistance = std::numeric_limits<float>::max();

  for (int i = 0; i < static_cast<int>(m_colorPalette.size()); ++i) {
    auto paletteColor = m_colorPalette[i];
    float distance = std::abs(paletteColor.getRed() - color.getRed()) +
                     std::abs(paletteColor.getGreen() - color.getGreen()) +
                     std::abs(paletteColor.getBlue() - color.getBlue());

    if (distance < minDistance) {
      minDistance = distance;
      m_selectedIndex = i;
    }
  }
  repaint();
}

juce::Colour ColorSwatchGrid::getColorAtIndex(int index) const {
  if (index >= 0 && index < static_cast<int>(m_colorPalette.size())) {
    return m_colorPalette[index];
  }
  return juce::Colours::black;
}

void ColorSwatchGrid::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds();

  // Draw background
  g.setColour(juce::Colour(0xff2a2a2a));
  g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

  // Draw border
  g.setColour(juce::Colour(0xff444444));
  g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

  // Calculate swatch dimensions (4:3 aspect ratio, increased spacing)
  const int padding = 6;
  const int swatchSpacing = 4; // Increased from 2px to 4px
  int availableWidth = bounds.getWidth() - (padding * 2);
  int availableHeight = bounds.getHeight() - (padding * 2);

  // 4:3 aspect ratio swatches (width is 4/3 of height)
  int swatchHeight = (availableHeight - (swatchSpacing * (ROWS - 1))) / ROWS;
  int swatchWidth = (swatchHeight * 4) / 3; // 4:3 ratio

  // Draw color swatches
  for (int row = 0; row < ROWS; ++row) {
    for (int col = 0; col < COLS; ++col) {
      int index = row * COLS + col;
      if (index < static_cast<int>(m_colorPalette.size())) {
        int x = padding + col * (swatchWidth + swatchSpacing);
        int y = padding + row * (swatchHeight + swatchSpacing);

        // Draw swatch
        g.setColour(m_colorPalette[index]);
        g.fillRect(x, y, swatchWidth, swatchHeight);

        // Draw selection border if this is the selected swatch
        if (index == m_selectedIndex) {
          g.setColour(juce::Colours::white);
          g.drawRect(x - 1, y - 1, swatchWidth + 2, swatchHeight + 2, 2);
        }
      }
    }
  }
}

void ColorSwatchGrid::mouseDown(const juce::MouseEvent& event) {
  int index = getSwatchIndexAt(event.x, event.y);

  if (index >= 0 && index < static_cast<int>(m_colorPalette.size())) {
    m_selectedIndex = index;

    if (onColorSelected) {
      onColorSelected(m_colorPalette[index]);
    }

    repaint();
  }
}

int ColorSwatchGrid::getSwatchIndexAt(int x, int y) const {
  auto bounds = getLocalBounds();

  const int padding = 6;
  const int swatchSpacing = 4; // Match paint() spacing
  int availableWidth = bounds.getWidth() - (padding * 2);
  int availableHeight = bounds.getHeight() - (padding * 2);

  // 4:3 aspect ratio swatches (must match paint() logic)
  int swatchHeight = (availableHeight - (swatchSpacing * (ROWS - 1))) / ROWS;
  int swatchWidth = (swatchHeight * 4) / 3; // 4:3 ratio

  // Check if click is within the swatch grid
  int relX = x - padding;
  int relY = y - padding;

  if (relX < 0 || relY < 0)
    return -1;

  // Calculate which swatch was clicked
  int col = relX / (swatchWidth + swatchSpacing);
  int row = relY / (swatchHeight + swatchSpacing);

  if (col >= COLS || row >= ROWS)
    return -1;

  // Verify click is within swatch bounds (not in spacing)
  int swatchX = col * (swatchWidth + swatchSpacing);
  int swatchY = row * (swatchHeight + swatchSpacing);

  if (relX < swatchX || relX >= swatchX + swatchWidth || relY < swatchY ||
      relY >= swatchY + swatchHeight) {
    return -1;
  }

  return row * COLS + col;
}

//==============================================================================
// ColorSwatchPicker implementation (compact button with popup)
//==============================================================================
ColorSwatchPicker::ColorSwatchPicker() {
  m_selectedColor = juce::Colours::red; // Default color
}

ColorSwatchPicker::~ColorSwatchPicker() {
  hideColorPopup();
}

void ColorSwatchPicker::setSelectedColor(const juce::Colour& color) {
  m_selectedColor = color;
  repaint();
}

void ColorSwatchPicker::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds();

  // Fill ENTIRE button background with clip's current color
  g.setColour(m_selectedColor);
  g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

  // Draw border (darker for contrast)
  g.setColour(juce::Colour(0xff222222));
  g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

  // Draw dropdown indicator (small triangle on right)
  // Use contrasting color based on brightness
  auto triangleArea = bounds.removeFromRight(20);
  juce::Path triangle;
  triangle.addTriangle(triangleArea.getCentreX() - 4.0f, triangleArea.getCentreY() - 2.0f,
                       triangleArea.getCentreX() + 4.0f, triangleArea.getCentreY() - 2.0f,
                       triangleArea.getCentreX(), triangleArea.getCentreY() + 3.0f);

  // Use white or black triangle depending on background brightness
  float brightness = m_selectedColor.getBrightness();
  g.setColour(brightness > 0.5f ? juce::Colours::black.withAlpha(0.7f)
                                : juce::Colours::white.withAlpha(0.7f));
  g.fillPath(triangle);
}

void ColorSwatchPicker::resized() {
  // Nothing needed here
}

void ColorSwatchPicker::mouseDown(const juce::MouseEvent& /*event*/) {
  if (m_isPopupVisible) {
    hideColorPopup();
  } else {
    showColorPopup();
  }
}

void ColorSwatchPicker::showColorPopup() {
  auto* grid = new ColorSwatchGrid();
  grid->setSelectedColor(m_selectedColor);

  // IMPORTANT: Don't capture 'grid' pointer - it will be owned and deleted by CallOutBox
  // Only capture 'this' to update the parent picker's color
  grid->onColorSelected = [this](const juce::Colour& color) {
    m_selectedColor = color;
    if (onColorSelected) {
      onColorSelected(color);
    }
    repaint();
    // Note: Don't call hideColorPopup() - CallOutBox manages its own lifetime
  };

  // Create popup hovering over the button (centered on parent)
  auto bounds = getScreenBounds();
  int popupWidth = 284;                                 // Tight fit for 4×12 grid
  int popupHeight = 80;                                 // 4 rows
  int popupX = bounds.getCentreX() - (popupWidth / 2);  // Center horizontally to parent
  int popupY = bounds.getCentreY() - (popupHeight / 2); // Center vertically (hover over button)
  juce::Rectangle<int> popupBounds(popupX, popupY, popupWidth, popupHeight);

  m_isPopupVisible = true;

  // CallOutBox takes ownership and will delete the grid when closed
  // Don't store it in m_popupHolder - that would cause double-delete
  juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(grid), popupBounds,
                                         nullptr);
}

void ColorSwatchPicker::hideColorPopup() {
  // CallOutBox manages its own lifetime, we just track visibility state
  m_isPopupVisible = false;
}
