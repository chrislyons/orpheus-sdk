// SPDX-License-Identifier: MIT

#include "ColorSwatchPicker.h"

//==============================================================================
// ColorSwatchGrid implementation (popup grid)
//==============================================================================
ColorSwatchGrid::ColorSwatchGrid() {
  initializeColorPalette();
  m_selectedIndex = 0;
  // 4:3 aspect swatches + 4px spacing, 4 rows (removed black row)
  // Calculate optimal width to eliminate dead space:
  // Height = 80px, padding = 6px × 2 = 12px
  // Available height = 80 - 12 = 68px
  // Swatch height = (68 - (3 × 4spacing)) / 4 rows = (68 - 12) / 4 = 14px
  // Swatch width = 14 × 4/3 = 18.67 ≈ 19px (rounded up)
  // Total width = 14swatches × 19px + 13spacing × 4px + 12padding = 266 + 52 + 12 = 330px
  setSize(330, 80); // Tighter fit, no wasted horizontal space
}

void ColorSwatchGrid::initializeColorPalette() {
  // Ableton-inspired color palette (4 rows × 14 columns, removed mostly-black row 5)
  m_colorPalette = {// Row 1
                    juce::Colour(0xffff9eb3), juce::Colour(0xffff8030), juce::Colour(0xffc89633),
                    juce::Colour(0xffffe766), juce::Colour(0xffc8ff66), juce::Colour(0xff7fff66),
                    juce::Colour(0xff66ffaa), juce::Colour(0xff66ffdd), juce::Colour(0xff99ccff),
                    juce::Colour(0xff9999ff), juce::Colour(0xffb399ff), juce::Colour(0xffcc99ff),
                    juce::Colour(0xffff99cc), juce::Colour(0xffffffff),
                    // Row 2
                    juce::Colour(0xffff3333), juce::Colour(0xffff8833), juce::Colour(0xffaa7733),
                    juce::Colour(0xffffff66), juce::Colour(0xffccff66), juce::Colour(0xff88ff66),
                    juce::Colour(0xff66ffbb), juce::Colour(0xff66ffff), juce::Colour(0xff66ccff),
                    juce::Colour(0xff6699ff), juce::Colour(0xff9966ff), juce::Colour(0xffcc66ff),
                    juce::Colour(0xffff66cc), juce::Colour(0xffcccccc),
                    // Row 3
                    juce::Colour(0xffff9999), juce::Colour(0xffffaa77), juce::Colour(0xffccaa88),
                    juce::Colour(0xffffff99), juce::Colour(0xffddff99), juce::Colour(0xffaaff99),
                    juce::Colour(0xff99ffcc), juce::Colour(0xff99ffff), juce::Colour(0xff99ddff),
                    juce::Colour(0xff99bbff), juce::Colour(0xffbb99ff), juce::Colour(0xffee99ff),
                    juce::Colour(0xffff99ee), juce::Colour(0xffaaaaaa),
                    // Row 4
                    juce::Colour(0xffdd6666), juce::Colour(0xffff7733), juce::Colour(0xff997755),
                    juce::Colour(0xffffdd44), juce::Colour(0xffaacc44), juce::Colour(0xff66cc44),
                    juce::Colour(0xff44ccaa), juce::Colour(0xff44cccc), juce::Colour(0xff4499dd),
                    juce::Colour(0xff4477dd), juce::Colour(0xff8844dd), juce::Colour(0xffcc44cc),
                    juce::Colour(0xffff44aa), juce::Colour(0xff888888)};
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

  // Draw button background
  g.setColour(juce::Colour(0xff3a3a3a));
  g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

  // Draw current color swatch (left side of button)
  auto colorSwatch = bounds.reduced(4).removeFromLeft(bounds.getHeight() - 8);
  g.setColour(m_selectedColor);
  g.fillRoundedRectangle(colorSwatch.toFloat(), 2.0f);

  // Draw border
  g.setColour(juce::Colour(0xff555555));
  g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

  // Draw dropdown indicator (small triangle on right)
  auto triangleArea = bounds.removeFromRight(20);
  juce::Path triangle;
  triangle.addTriangle(triangleArea.getCentreX() - 4.0f, triangleArea.getCentreY() - 2.0f,
                       triangleArea.getCentreX() + 4.0f, triangleArea.getCentreY() - 2.0f,
                       triangleArea.getCentreX(), triangleArea.getCentreY() + 3.0f);
  g.setColour(juce::Colours::white.withAlpha(0.7f));
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
  int popupWidth = 330;                                 // Optimized width (no wasted space)
  int popupHeight = 80;                                 // 4 rows instead of 5
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
