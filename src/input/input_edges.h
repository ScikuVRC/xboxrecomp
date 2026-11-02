/* Bounded button-transition retention, independent of any input/window API.
 * Caller supplies synchronization and a monotonic nanosecond clock. */
#ifndef XBOX_INPUT_EDGES_H
#define XBOX_INPUT_EDGES_H
#include <stdint.h>
#define INPUT_EDGES_CAPACITY 64
#define INPUT_EDGES_MAX_AGE_NS 100000000ull
typedef struct InputEdge {
    uint64_t time_ns, token;
    unsigned char buttons[10]; /* Xbox body: digital WORD + eight analog buttons. */
    int delivered;
} InputEdge;
typedef struct InputEdges {
    InputEdge entries[INPUT_EDGES_CAPACITY];
    unsigned head, count;
    unsigned char latest[10], acknowledged[10];
    uint64_t next_token, overflow, expired, cancelled, generation;
    uint32_t packet;
    unsigned analog_threshold;
    int packet_seen;
} InputEdges;
void input_edges_reset(InputEdges *q, const unsigned char buttons[10]);
void input_edges_feed(InputEdges *q, const unsigned char buttons[10], uint64_t now);
uint64_t input_edges_peek(InputEdges *q, unsigned char buttons[10], uint64_t now);
void input_edges_delivered(InputEdges *q, uint64_t token);
uint64_t input_edges_consume(InputEdges *q, uint32_t packet, const unsigned char buttons[10], uint64_t now);
uint32_t input_edges_mask(const unsigned char buttons[10]);
#endif
