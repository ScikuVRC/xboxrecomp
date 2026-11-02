#include "input_edges.h"
#include <string.h>

uint32_t input_edges_mask(const unsigned char b[10])
{
    uint32_t mask = (uint32_t)b[0] | (uint32_t)b[1] << 8;
    unsigned i;
    for (i = 2; i < 10; i++) if (b[i]) mask |= 1u << (i + 14);
    return mask;
}

static uint32_t mask_for(const InputEdges *q, const unsigned char b[10])
{
    uint32_t mask = (uint32_t)b[0] | (uint32_t)b[1] << 8;
    unsigned i;
    for (i = 2; i < 10; i++) if (b[i] > q->analog_threshold) mask |= 1u << (i + 14);
    return mask;
}

void input_edges_reset(InputEdges *q, const unsigned char buttons[10])
{
    q->cancelled += q->count;
    q->head = q->count = 0;
    memcpy(q->latest, buttons, 10);
    memcpy(q->acknowledged, buttons, 10);
    q->packet_seen = 0;
    q->generation++;
    /* Tokens never repeat on reset: an older in-flight TD cannot commit anew. */
}

static void expire(InputEdges *q, uint64_t now)
{
    if (q->count && now - q->entries[q->head].time_ns > INPUT_EDGES_MAX_AGE_NS) {
        q->expired += q->count;
        q->generation++;
        q->head = q->count = 0;
        memcpy(q->acknowledged, q->latest, 10);
    }
}

void input_edges_feed(InputEdges *q, const unsigned char buttons[10], uint64_t now)
{
    InputEdge *entry;
    uint32_t old = mask_for(q, q->latest);
    expire(q, now);
    memcpy(q->latest, buttons, 10);
    if (old == mask_for(q, buttons)) return; /* Analog magnitude remains current. */
    q->next_token++;
    if (q->count == INPUT_EDGES_CAPACITY) {
        q->overflow += q->count + 1u;
        q->generation++;
        q->head = q->count = 0;
        memcpy(q->acknowledged, buttons, 10);
        return; /* Resynchronize instead of playing old commands later. */
    }
    entry = &q->entries[(q->head + q->count++) % INPUT_EDGES_CAPACITY];
    entry->time_ns = now;
    entry->token = q->next_token;
    entry->delivered = 0;
    memcpy(entry->buttons, buttons, 10);
}

uint64_t input_edges_peek(InputEdges *q, unsigned char buttons[10], uint64_t now)
{
    uint32_t changed = 0, previous = mask_for(q, q->acknowledged);
    uint64_t token = 0;
    unsigned i;
    expire(q, now);
    memcpy(buttons, q->latest, 10);
    /* Coalesce independent changes (e.g. A then B becomes the A+B chord),
     * but never cross two transitions of the same button before consumption. */
    for (i = 0; i < q->count; i++) {
        InputEdge *entry = &q->entries[(q->head + i) % INPUT_EDGES_CAPACITY];
        uint32_t mask = mask_for(q, entry->buttons), diff = previous ^ mask;
        if (changed & diff) break;
        changed |= diff;
        previous = mask;
        memcpy(buttons, entry->buttons, 10);
        token = entry->token;
    }
    for (i = 2; i < 10; i++)
        if ((buttons[i] > q->analog_threshold) == (q->latest[i] > q->analog_threshold))
            buttons[i] = q->latest[i];
    return token;
}

void input_edges_delivered(InputEdges *q, uint64_t token)
{
    unsigned i;
    if (!token) return;
    for (i = 0; i < q->count; i++) {
        InputEdge *entry = &q->entries[(q->head + i) % INPUT_EDGES_CAPACITY];
        if (entry->token == token) { entry->delivered = 1; return; }
    }
}

uint64_t input_edges_consume(InputEdges *q, uint32_t packet, const unsigned char buttons[10], uint64_t now)
{
    unsigned i;
    uint32_t mask = mask_for(q, buttons);
    expire(q, now);
    /* Re-reading an old guest packet does not acknowledge a later cycle. */
    if (q->packet_seen && (int32_t)(packet - q->packet) <= 0) return 0;
    q->packet_seen = 1;
    q->packet = packet;
    for (i = 0; i < q->count; i++) {
        InputEdge *entry = &q->entries[(q->head + i) % INPUT_EDGES_CAPACITY];
        if (entry->delivered && mask_for(q, entry->buttons) == mask) {
            uint64_t token = entry->token;
            memcpy(q->acknowledged, entry->buttons, 10);
            q->head = (q->head + i + 1u) % INPUT_EDGES_CAPACITY;
            q->count -= i + 1u;
            return token;
        }
    }
    return 0;
}
