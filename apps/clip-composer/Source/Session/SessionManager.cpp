// SPDX-License-Identifier: MIT

#include "SessionManager.h"
#include <juce_audio_formats/juce_audio_formats.h>

//==============================================================================
SessionManager::SessionManager() {
  // Initialize default tab labels
  for (int i = 0; i < NUM_TABS; ++i) {
    m_tabLabels[i] = (juce::String("Tab ") + juce::String(i + 1)).toStdString();
  }
}

//==============================================================================
void SessionManager::setActiveTab(int tabIndex) {
  if (tabIndex >= 0 && tabIndex < NUM_TABS) {
    m_currentTab = tabIndex;
    DBG("SessionManager: Active tab set to " << tabIndex);
  }
}

std::string SessionManager::getTabLabel(int tabIndex) const {
  if (tabIndex >= 0 && tabIndex < NUM_TABS)
    return m_tabLabels[tabIndex];
  return "";
}

void SessionManager::setTabLabel(int tabIndex, const std::string& label) {
  if (tabIndex >= 0 && tabIndex < NUM_TABS) {
    m_tabLabels[tabIndex] = label;
    DBG("SessionManager: Tab " << tabIndex << " label set to: " << label);
  }
}

//==============================================================================
bool SessionManager::loadClip(int buttonIndex, const juce::String& filePath) {
  juce::File audioFile(filePath);

  // Validate file exists
  if (!audioFile.existsAsFile()) {
    DBG("SessionManager: File not found: " << filePath);
    return false;
  }

  // Extract metadata
  auto clipData = extractMetadata(filePath);

  if (!clipData.isValid()) {
    DBG("SessionManager: Failed to read metadata from: " << filePath);
    return false;
  }

  // Set tab index
  clipData.tabIndex = m_currentTab;

  // Store in map with composite key (tab, button)
  int key = makeKey(m_currentTab, buttonIndex);
  m_clips[key] = clipData;

  DBG("SessionManager: Loaded clip " << clipData.displayName << " onto tab " << m_currentTab
                                     << ", button " << buttonIndex << " (" << clipData.sampleRate
                                     << " Hz, " << clipData.numChannels << " ch, "
                                     << clipData.durationSamples << " samples)");

  return true;
}

void SessionManager::setClip(int buttonIndex, const ClipData& clipData) {
  int key = makeKey(m_currentTab, buttonIndex);
  m_clips[key] = clipData;

  DBG("SessionManager: Updated clip metadata for tab " << m_currentTab << ", button " << buttonIndex
                                                       << " - Name: " << clipData.displayName
                                                       << ", Group: " << clipData.clipGroup);
}

void SessionManager::removeClip(int buttonIndex) {
  int key = makeKey(m_currentTab, buttonIndex);
  auto it = m_clips.find(key);
  if (it != m_clips.end()) {
    DBG("SessionManager: Removed clip from tab " << m_currentTab << ", button " << buttonIndex);
    m_clips.erase(it);
  }
}

void SessionManager::swapClips(int buttonIndex1, int buttonIndex2) {
  int key1 = makeKey(m_currentTab, buttonIndex1);
  int key2 = makeKey(m_currentTab, buttonIndex2);

  auto it1 = m_clips.find(key1);
  auto it2 = m_clips.find(key2);

  bool has1 = (it1 != m_clips.end());
  bool has2 = (it2 != m_clips.end());

  if (has1 && has2) {
    // Both buttons have clips - swap them
    std::swap(it1->second, it2->second);
    DBG("SessionManager: Swapped clips between buttons " << buttonIndex1 << " and "
                                                         << buttonIndex2);
  } else if (has1 && !has2) {
    // Only button 1 has a clip - move it to button 2
    m_clips[key2] = it1->second;
    m_clips.erase(it1);
    DBG("SessionManager: Moved clip from button " << buttonIndex1 << " to " << buttonIndex2);
  } else if (!has1 && has2) {
    // Only button 2 has a clip - move it to button 1
    m_clips[key1] = it2->second;
    m_clips.erase(it2);
    DBG("SessionManager: Moved clip from button " << buttonIndex2 << " to " << buttonIndex1);
  } else {
    // Neither button has a clip - nothing to swap
    DBG("SessionManager: No clips to swap (both buttons empty)");
  }
}

SessionManager::ClipData SessionManager::getClip(int buttonIndex) const {
  int key = makeKey(m_currentTab, buttonIndex);
  auto it = m_clips.find(key);
  if (it != m_clips.end())
    return it->second;

  return ClipData(); // Empty/invalid
}

bool SessionManager::hasClip(int buttonIndex) const {
  int key = makeKey(m_currentTab, buttonIndex);
  return m_clips.find(key) != m_clips.end();
}

//==============================================================================
SessionManager::ClipData SessionManager::extractMetadata(const juce::String& filePath) {
  ClipData data;
  data.filePath = filePath.toStdString();

  juce::File file(filePath);

  // Display name: filename without extension
  data.displayName = file.getFileNameWithoutExtension().toStdString();

  // Color based on file extension (basic heuristic)
  auto ext = file.getFileExtension().toLowerCase();
  if (ext == ".wav")
    data.color = juce::Colours::blue;
  else if (ext == ".aiff" || ext == ".aif")
    data.color = juce::Colours::green;
  else if (ext == ".flac")
    data.color = juce::Colours::purple;
  else
    data.color = juce::Colours::grey;

  // Read audio metadata using JUCE
  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats(); // WAV, AIFF, FLAC

  std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

  if (reader) {
    data.sampleRate = static_cast<int>(reader->sampleRate);
    data.numChannels = static_cast<int>(reader->numChannels);
    data.durationSamples = reader->lengthInSamples;

    // Initialize trim points to full duration
    data.trimInSamples = 0;
    data.trimOutSamples = data.durationSamples;
  } else {
    DBG("SessionManager: Failed to create reader for: " << filePath);
    data.filePath.clear(); // Mark as invalid
  }

  return data;
}

//==============================================================================
bool SessionManager::saveSession(const juce::File& file) {
  // Create JSON structure
  juce::var sessionJson = juce::var(new juce::DynamicObject());
  auto* sessionObj = sessionJson.getDynamicObject();

  sessionObj->setProperty("name", juce::var(m_sessionName));
  sessionObj->setProperty("version", juce::var("0.2.0")); // Bumped for multi-tab support

  // Tab labels array
  juce::Array<juce::var> tabLabelsArray;
  for (int i = 0; i < NUM_TABS; ++i) {
    tabLabelsArray.add(juce::var(m_tabLabels[i]));
  }
  sessionObj->setProperty("tabLabels", juce::var(tabLabelsArray));

  // Clips array
  juce::Array<juce::var> clipsArray;

  for (const auto& [compositeKey, clipData] : m_clips) {
    juce::var clipJson = juce::var(new juce::DynamicObject());
    auto* clipObj = clipJson.getDynamicObject();

    // Extract button index from composite key (compositeKey % 100)
    int buttonIndex = compositeKey % 100;

    clipObj->setProperty("tabIndex", juce::var(clipData.tabIndex));
    clipObj->setProperty("buttonIndex", juce::var(buttonIndex));
    clipObj->setProperty("filePath", juce::var(clipData.filePath));
    clipObj->setProperty("displayName", juce::var(clipData.displayName));
    clipObj->setProperty("clipGroup", juce::var(clipData.clipGroup));

    // Phase 2: Trim points
    clipObj->setProperty("trimInSamples",
                         juce::var(static_cast<juce::int64>(clipData.trimInSamples)));
    clipObj->setProperty("trimOutSamples",
                         juce::var(static_cast<juce::int64>(clipData.trimOutSamples)));

    // Phase 3: Fade times
    clipObj->setProperty("fadeInSeconds", juce::var(clipData.fadeInSeconds));
    clipObj->setProperty("fadeOutSeconds", juce::var(clipData.fadeOutSeconds));
    clipObj->setProperty("fadeInCurve", juce::var(clipData.fadeInCurve));
    clipObj->setProperty("fadeOutCurve", juce::var(clipData.fadeOutCurve));

    clipsArray.add(clipJson);
  }

  sessionObj->setProperty("clips", juce::var(clipsArray));

  // Write to file
  juce::String jsonString = juce::JSON::toString(sessionJson, true); // Pretty print

  if (file.replaceWithText(jsonString)) {
    m_currentFile = file;
    DBG("SessionManager: Saved session to: " << file.getFullPathName());
    return true;
  }

  DBG("SessionManager: Failed to write session file: " << file.getFullPathName());
  return false;
}

bool SessionManager::loadSession(const juce::File& file) {
  if (!file.existsAsFile()) {
    DBG("SessionManager: Session file not found: " << file.getFullPathName());
    return false;
  }

  // Parse JSON
  juce::String jsonText = file.loadFileAsString();
  juce::var sessionJson = juce::JSON::parse(jsonText);

  if (!sessionJson.isObject()) {
    DBG("SessionManager: Invalid JSON in session file");
    return false;
  }

  // Clear current session
  clearSession();

  // Load session metadata
  auto* sessionObj = sessionJson.getDynamicObject();
  if (sessionObj) {
    m_sessionName = sessionObj->getProperty("name").toString().toStdString();

    // Load tab labels if present (version 0.2.0+)
    auto tabLabelsArray = sessionObj->getProperty("tabLabels");
    if (tabLabelsArray.isArray()) {
      for (int i = 0; i < juce::jmin(NUM_TABS, tabLabelsArray.size()); ++i) {
        m_tabLabels[i] = tabLabelsArray[i].toString().toStdString();
      }
    }
  }

  // Load clips
  auto clipsArray = sessionObj->getProperty("clips");
  if (clipsArray.isArray()) {
    for (int i = 0; i < clipsArray.size(); ++i) {
      auto clipJson = clipsArray[i];
      if (!clipJson.isObject())
        continue;

      auto* clipObj = clipJson.getDynamicObject();
      int tabIndex = clipObj->getProperty("tabIndex");
      int buttonIndex = clipObj->getProperty("buttonIndex");
      juce::String filePath = clipObj->getProperty("filePath").toString();

      // Set active tab temporarily to load clip to correct tab
      int savedTab = m_currentTab;
      m_currentTab = tabIndex;

      // Load clip (validates file and extracts metadata)
      if (loadClip(buttonIndex, filePath)) {
        // Restore additional metadata from session
        int key = makeKey(tabIndex, buttonIndex);
        auto& clipData = m_clips[key];

        // Restore display name and clip group
        if (clipObj->hasProperty("displayName")) {
          clipData.displayName = clipObj->getProperty("displayName").toString().toStdString();
        }
        if (clipObj->hasProperty("clipGroup")) {
          clipData.clipGroup = static_cast<int>(clipObj->getProperty("clipGroup"));
        }

        // Phase 2: Restore trim points
        if (clipObj->hasProperty("trimInSamples")) {
          clipData.trimInSamples = static_cast<int64_t>(clipObj->getProperty("trimInSamples"));
        }
        if (clipObj->hasProperty("trimOutSamples")) {
          clipData.trimOutSamples = static_cast<int64_t>(clipObj->getProperty("trimOutSamples"));
        }

        // Phase 3: Restore fade times
        if (clipObj->hasProperty("fadeInSeconds")) {
          clipData.fadeInSeconds = static_cast<double>(clipObj->getProperty("fadeInSeconds"));
        }
        if (clipObj->hasProperty("fadeOutSeconds")) {
          clipData.fadeOutSeconds = static_cast<double>(clipObj->getProperty("fadeOutSeconds"));
        }
        if (clipObj->hasProperty("fadeInCurve")) {
          clipData.fadeInCurve = clipObj->getProperty("fadeInCurve").toString().toStdString();
        }
        if (clipObj->hasProperty("fadeOutCurve")) {
          clipData.fadeOutCurve = clipObj->getProperty("fadeOutCurve").toString().toStdString();
        }
      }

      // Restore original active tab
      m_currentTab = savedTab;
    }
  }

  m_currentFile = file;
  DBG("SessionManager: Loaded session from: " << file.getFullPathName());
  return true;
}

void SessionManager::clearSession() {
  m_clips.clear();
  m_sessionName = "Untitled";
  m_currentFile = juce::File();

  // Reset tab labels to defaults
  for (int i = 0; i < NUM_TABS; ++i) {
    m_tabLabels[i] = (juce::String("Tab ") + juce::String(i + 1)).toStdString();
  }

  DBG("SessionManager: Cleared session");
}
