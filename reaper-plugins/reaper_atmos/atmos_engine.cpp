#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include "atmos_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct AtmosChannelDest {
  bool is_object = false;
  int index = 0;
};

class AtmosRouter {
public:
  void setChannels(int nch) { m_map.assign(nch, AtmosChannelDest{}); }

  void mapChannelToBed(int ch, int bedIndex)
  {
    if (ch >= 0 && ch < static_cast<int>(m_map.size()))
      m_map[ch] = AtmosChannelDest{false, bedIndex};
  }

  void mapChannelToObject(int ch, int objectIndex)
  {
    if (ch >= 0 && ch < static_cast<int>(m_map.size()))
      m_map[ch] = AtmosChannelDest{true, objectIndex};
  }

  void reset()
  {
    m_map.clear();
    m_beds.clear();
    m_objects.clear();
  }

  void processBlock(PCM_source_transfer_t* block)
  {
    if (!block || !block->samples || block->length <= 0 || block->nch <= 0)
      return;

    const int nch = block->nch;
    const int len = block->length;
    if (static_cast<int>(m_map.size()) < nch)
      m_map.resize(nch, AtmosChannelDest{});

    if (m_beds.empty())
      m_beds.resize(16);
    if (m_objects.empty())
      m_objects.resize(128);

    for (int ch = 0; ch < nch; ++ch)
    {
      ReaSample* src = block->samples + static_cast<size_t>(ch) * len;
      const AtmosChannelDest& dest = m_map[ch];
      std::vector<ReaSample>& buffer = dest.is_object ? m_objects[dest.index]
                                                      : m_beds[dest.index];
      buffer.assign(src, src + len);
    }
  }

  const std::vector<ReaSample>& getBed(int idx) const { return m_beds.at(idx); }
  const std::vector<ReaSample>& getObject(int idx) const { return m_objects.at(idx); }

private:
  std::vector<AtmosChannelDest> m_map;
  std::vector<std::vector<ReaSample>> m_beds;
  std::vector<std::vector<ReaSample>> m_objects;
};

static AtmosRouter g_router;

struct BuiltinFormat {
  const char* name;
  const char* channels[16];
};

static constexpr BuiltinFormat g_builtin_formats[] = {
  {"5.1.4", {"L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf", "Ltr", "Rtr", nullptr}},
  {"7.1.2", {"L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs", "Ltf", "Rtf", nullptr}},
};

static std::vector<reaper_atmos_speaker_format> g_formats;
static std::map<MediaTrack*, int> g_track_to_object;

void ensure_formats_initialized()
{
  if (!g_formats.empty())
    return;

  for (const auto& fmt : g_builtin_formats)
  {
    reaper_atmos_speaker_format entry{};
    entry.name = fmt.name;

    int channel_count = 0;
    while (fmt.channels[channel_count])
      ++channel_count;

    entry.num_channels = channel_count;
    entry.channel_names = fmt.channels;
    g_formats.push_back(entry);
  }
}

}  // namespace

void AtmosEngine_Initialize()
{
  ensure_formats_initialized();
  g_router.reset();
  g_track_to_object.clear();
}

void AtmosEngine_Shutdown()
{
  g_router.reset();
  g_track_to_object.clear();
  g_formats.clear();
}

void REAPER_API_DECL Atmos_RegisterSpeakerFormat(const reaper_atmos_speaker_format* fmt)
{
  ensure_formats_initialized();
  if (fmt)
    g_formats.push_back(*fmt);
}

int REAPER_API_DECL Atmos_GetSpeakerFormatCount()
{
  ensure_formats_initialized();
  return static_cast<int>(g_formats.size());
}

const reaper_atmos_speaker_format* REAPER_API_DECL Atmos_GetSpeakerFormat(int idx)
{
  ensure_formats_initialized();
  if (idx < 0 || idx >= static_cast<int>(g_formats.size()))
    return nullptr;
  return &g_formats[idx];
}

void REAPER_API_DECL Atmos_AssignTrackObject(MediaTrack* track, int object_id)
{
  if (!track)
    return;
  g_track_to_object[track] = object_id;
}

int REAPER_API_DECL Atmos_GetTrackObject(MediaTrack* track)
{
  if (!track)
    return -1;
  const auto it = g_track_to_object.find(track);
  if (it == g_track_to_object.end())
    return -1;
  return it->second;
}

namespace {

bool write_text_file(const char* path, const char* text)
{
  if (!path || !text)
    return false;

  FILE* fp = std::fopen(path, "wb");
  if (!fp)
    return false;

  const size_t len = std::strlen(text);
  const size_t written = std::fwrite(text, 1, len, fp);
  std::fclose(fp);
  return written == len;
}

}  // namespace

bool REAPER_API_DECL Atmos_ExportADM(const char* path)
{
  return write_text_file(path, "ADM export placeholder\n");
}

bool REAPER_API_DECL Atmos_ExportBWF(const char* path)
{
  return write_text_file(path, "BWF export placeholder\n");
}
