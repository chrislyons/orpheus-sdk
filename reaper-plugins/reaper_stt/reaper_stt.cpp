#include <cstring>
#include "stt_stubs.h"
#include "../../sdk/reaper_plugin.h"
#include "../../sdk/reaper_plugin_functions.h"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <mutex>
#include "stt_engine.h"

/*
  Simple speech-to-text helper using PCM_source_transfer_t blocks.
  This example feeds blocks into a local STT engine (stubbed) and
  inserts project markers for each recognized word while maintaining
  a parallel text lane for basic word-level search/editing.
*/

// ----------------------------------------------------------------------
// STT engine abstraction
// ----------------------------------------------------------------------
namespace {

// Default stub implementation used when no real engine is provided.
class StubEngine : public STTEngine {
public:
  std::string Transcribe(const ReaSample* samples,
                         int nch, int length, double samplerate) override
  {
    // In real use, connect to an actual STT engine.
    // Here we return a placeholder string for demonstration.
    (void)samples; (void)nch; (void)length; (void)samplerate;
    return "hello world"; // dummy transcription
  }
};

static StubEngine g_stub_engine;
static STTEngine* g_engine = &g_stub_engine;
static std::mutex g_engine_mutex;

constexpr const char kApiTranscribeSource[] = "API_TranscribeSource";
constexpr const char kApiFindWord[] = "API_STT_FindWord";
constexpr const char kApiReplaceWord[] = "API_STT_ReplaceWord";
constexpr const char kApiSetEngine[] = "API_STT_SetEngine";

bool g_api_registered = false;
int (*g_register_fn)(const char*, void*) = nullptr;

static std::string run_stt(const ReaSample* samples,
                           int nch, int length, double samplerate)
{
  std::lock_guard<std::mutex> lock(g_engine_mutex);
  return g_engine->Transcribe(samples, nch, length, samplerate);
}

} // namespace

void STT_SetEngine(STTEngine* engine)
{
  std::lock_guard<std::mutex> lock(g_engine_mutex);
  g_engine = engine ? engine : &g_stub_engine;
}

// ----------------------------------------------------------------------
// Text lane structure
// ----------------------------------------------------------------------
struct WordEntry {
  std::string word;
  double position; // seconds
};

class TextLane {
public:
  void addWord(const std::string& word, double position)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    words_.push_back({word, position});
  }

  int findWord(const std::string& word)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < words_.size(); ++i)
      if (words_[i].word == word) return (int)i;
    return -1;
  }

  void replaceWord(const std::string& oldWord, const std::string& newWord)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : words_)
      if (w.word == oldWord) w.word = newWord;
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    words_.clear();
  }

private:
  std::vector<WordEntry> words_;
  std::mutex mutex_;
};

static TextLane g_lane;

// ----------------------------------------------------------------------
// Feed a PCM block to STT and insert markers
// ----------------------------------------------------------------------
static void feed_block_to_stt(PCM_source_transfer_t *block, double start_time)
{
  if (!block || !block->samples || block->samplerate <= 0.0)
    return;

  std::string text = run_stt(block->samples, block->nch, block->length, block->samplerate);
  std::istringstream iss(text);
  std::string word;
  const double word_dur = block->samplerate > 0.0
                              ? static_cast<double>(block->length) / block->samplerate
                              : 0.0;
  int word_index = 0;
  while (iss >> word)
  {
    double pos = start_time + word_index * word_dur;
    AddProjectMarker(nullptr, false, pos, pos, word.c_str(), -1);
    g_lane.addWord(word, pos);
    ++word_index;
  }
}

// ----------------------------------------------------------------------
// Public helper to process a PCM_source
// ----------------------------------------------------------------------
void TranscribeSource(PCM_source *src)
{
  if (!src) return;
  g_lane.clear();
  const int block_len = 4096;
  const int channel_count = src->GetNumChannels();
  const double sample_rate = src->GetSampleRate();
  if (channel_count <= 0 || sample_rate <= 0.0)
    return;

  std::vector<ReaSample> buffer(static_cast<size_t>(block_len) * channel_count);
  PCM_source_transfer_t block{};
  block.samples = buffer.data();
  block.length = block_len;
  block.nch = channel_count;
  block.samplerate = sample_rate;
  block.time_s = 0.0;
  double t = 0.0;
  while (true)
  {
    block.time_s = t;
    src->GetSamples(&block);
    if (block.samples_out <= 0) break;
    feed_block_to_stt(&block, t);
    t += (double)block.samples_out / block.samplerate;
  }
}

// ----------------------------------------------------------------------
// Search/edit API
// ----------------------------------------------------------------------
int STT_FindWord(const char *word)
{
  return g_lane.findWord(word ? word : "");
}

void STT_ReplaceWord(const char *oldWord, const char *newWord)
{
  if (!oldWord || !newWord) return;
  g_lane.replaceWord(oldWord, newWord);
}

extern "C" {
REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec)
{
  if (rec)
  {
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->Register || !rec->GetFunc)
      return 0;
    if (!REAPERAPI_LoadAPI(rec->GetFunc))
      return 0;
    if (!AddProjectMarker)
      return 0;

    g_register_fn = rec->Register;
    if (!g_register_fn)
      return 0;

    if (!g_register_fn(kApiTranscribeSource, (void*)TranscribeSource))
    {
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn(kApiFindWord, (void*)STT_FindWord))
    {
      g_register_fn("-API_TranscribeSource", (void*)TranscribeSource);
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn(kApiReplaceWord, (void*)STT_ReplaceWord))
    {
      g_register_fn("-API_STT_FindWord", (void*)STT_FindWord);
      g_register_fn("-API_TranscribeSource", (void*)TranscribeSource);
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn(kApiSetEngine, (void*)STT_SetEngine))
    {
      g_register_fn("-API_STT_ReplaceWord", (void*)STT_ReplaceWord);
      g_register_fn("-API_STT_FindWord", (void*)STT_FindWord);
      g_register_fn("-API_TranscribeSource", (void*)TranscribeSource);
      g_register_fn = nullptr;
      return 0;
    }

    g_api_registered = true;
    return 1;
  }

  if (g_api_registered && g_register_fn)
  {
    g_register_fn("-API_STT_SetEngine", (void*)STT_SetEngine);
    g_register_fn("-API_STT_ReplaceWord", (void*)STT_ReplaceWord);
    g_register_fn("-API_STT_FindWord", (void*)STT_FindWord);
    g_register_fn("-API_TranscribeSource", (void*)TranscribeSource);
  }

  g_lane.clear();
  STT_SetEngine(nullptr);
  g_api_registered = false;
  g_register_fn = nullptr;
  return 0;
}
}

