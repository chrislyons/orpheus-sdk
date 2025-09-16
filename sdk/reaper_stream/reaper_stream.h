#ifndef REAPER_STREAM_H
#define REAPER_STREAM_H

#include "pcm_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Open a streaming connection. The URL scheme selects the transport.
// Currently supported schemes: ws:// (and wss:// when TLS support is
// enabled at build time).
// Returns a non-zero handle on success.
int stream_open(const char *url);

// Send an audio block over the stream. Returns non-zero on success.
int stream_send(int handle, const PCM_source_transfer_t *block);

// Receive an audio block from the stream. The caller must allocate a buffer
// large enough to hold the requested number of samples. Returns the number of
// sample pairs received.
int stream_receive(int handle, PCM_source_transfer_t *block);

// Close an open stream handle. Returns non-zero when the handle was valid and
// has been released.
int stream_close(int handle);

// Copy the last error message associated with a handle into the supplied
// buffer. The returned value is the length of the full error string (without
// the terminating null). When the buffer is null or too small, the message is
// truncated but the full length is still reported. Returns 0 when no error is
// latched or the handle is invalid.
size_t stream_last_error(int handle, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
