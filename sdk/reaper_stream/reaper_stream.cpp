#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include "reaper_stream.h"

#include <algorithm>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

struct BlockData {
  PCM_source_transfer_t meta;
  std::vector<ReaSample> samples;
};

struct StreamConnection {
  enum Type { WS, SRT } type;
  std::mutex mtx;
  std::queue<BlockData> incoming; // simple loopback queue
};

static std::unordered_map<int, StreamConnection> g_streams;
static int g_nextHandle = 1;

static StreamConnection::Type parseType(const std::string &url)
{
  if (url.rfind("srt://",0) == 0) return StreamConnection::SRT;
  return StreamConnection::WS;
}

int stream_open(const char *url)
{
  if (!url) return 0;
  StreamConnection conn;
  conn.type = parseType(url);
  int handle = g_nextHandle++;
  g_streams.emplace(handle, std::move(conn));
  return handle;
}

int stream_send(int handle, const PCM_source_transfer_t *block)
{
  auto it = g_streams.find(handle);
  if (it == g_streams.end() || !block || !block->samples) return 0;

  BlockData bd;
  bd.meta = *block;
  const size_t sample_frames = static_cast<size_t>(block->length);
  const size_t channel_count = static_cast<size_t>(block->nch);
  const size_t total_samples = sample_frames * channel_count;

  bd.samples.assign(block->samples, block->samples + total_samples);
  bd.meta.samples = bd.samples.data();

  {
    std::lock_guard<std::mutex> lock(it->second.mtx);
    it->second.incoming.push(std::move(bd));
  }
  return 1;
}

int stream_receive(int handle, PCM_source_transfer_t *block)
{
  auto it = g_streams.find(handle);
  if (it == g_streams.end() || !block || !block->samples) return 0;
  ReaSample* dest_samples = block->samples;
  const int dest_length = block->length;
  std::lock_guard<std::mutex> lock(it->second.mtx);
  if (it->second.incoming.empty()) return 0;

  BlockData bd = std::move(it->second.incoming.front());
  it->second.incoming.pop();

  if (block->nch > 0 && block->nch != bd.meta.nch)
    return 0;

  const size_t channel_count = static_cast<size_t>(bd.meta.nch);
  const size_t requested_samples = static_cast<size_t>(dest_length) * channel_count;
  const size_t available_samples = bd.samples.size();
  const size_t copy_samples = std::min(requested_samples, available_samples);

  if (copy_samples > 0)
    std::memcpy(dest_samples, bd.samples.data(), copy_samples * sizeof(ReaSample));

  PCM_source_transfer_t meta = bd.meta;
  meta.samples = dest_samples;
  meta.length = dest_length;
  meta.samples_out = (channel_count > 0)
                         ? static_cast<int>(copy_samples / channel_count)
                         : 0;

  *block = meta;
  return block->samples_out;
}
