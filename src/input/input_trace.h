/* Optional observational input trace. No clock, allocation or IO when no sink.
 * Register before starting input threads. Payloads are borrowed for the call.
 * Stage names are static strings. The sink must not block or call input APIs. */
#ifndef XBOX_INPUT_TRACE_H
#define XBOX_INPUT_TRACE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*XboxInputTraceSink)(const char *stage, int slot, const void *data,
                                  unsigned bytes, uint64_t aux0, uint64_t aux1);
void xbox_InputTraceSetSink(XboxInputTraceSink sink);
int xbox_InputTraceActive(void);
void xbox_InputTraceEmit(const char *stage, int slot, const void *data,
                         unsigned bytes, uint64_t aux0, uint64_t aux1);
#ifdef __cplusplus
}
#endif
#endif
