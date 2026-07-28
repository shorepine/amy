#include "sequencer.h"
#include "amy.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint32_t sequencer_ticks() { return amy_global.sequencer_tick_count; }

// Both the sequencer and timed ('t' in the future) events are stored the same
// way: as the raw wire-message string (with its t/H command stripped) plus the
// scheduling metadata needed to play it back.  The string is only parsed when
// the entry comes due, so bulk ingest of scheduled events is just a scan and
// a copy -- no event parsing, voice allocation or delta expansion up front.
typedef struct sequence_info_t {
    char *wire;    // Stored wire message; NULL means the tag is unused.
    //uint32_t tag;  // tag is implicit, it's its index in the table
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
} sequence_info_t;

// One-shot messages scheduled at an absolute ms time ('t' in the future),
// kept in a time-sorted list so the per-block check is O(1) when nothing is
// due.  Wire-sourced entries hold the raw string; C-API-sourced entries hold
// a copy of the parsed event (no lossy string round-trip); MIDI-sourced
// entries hold the raw MIDI bytes so mapping lookup and template expansion
// happen against the synth state at play time, not at ingest.
typedef struct timed_wire_t {
    char *wire;            // wire-sourced entry, or...
    amy_event *event;      // ...event-sourced entry, or...
    uint8_t midi_len;      // ...nonzero for a raw-MIDI entry
    uint8_t midi_bytes[3];
    uint32_t time;         // absolute ms on the wire clock (amy_sysclock)
    struct timed_wire_t *next;
} timed_wire_t;

struct sequence_info_t *sequences = NULL;  // An array indexed by tag.
int32_t max_sequences = 0;
int32_t highest_tag = -1;
static timed_wire_t *timed_wire_head = NULL;
static volatile bool sequencer_running = true;
static volatile bool sequencer_external_clock = false;
// Firing a stored message parses it, and parsing (e.g. a patch load) can call
// amy_execute_deltas(), which calls back into the check functions below.  This
// flag makes those nested calls no-ops so a tick is never processed twice.
static volatile bool wire_firing = false;

void sequencer_init(int max_sequencer_tags) {
    // These are statics, so a stop/start of AMY within one process needs them
    // put back to their boot state (internal clock, running).
    sequencer_running = true;
    sequencer_external_clock = false;
    wire_firing = false;
    max_sequences = max_sequencer_tags;
    sequences = (struct sequence_info_t *)malloc_caps(max_sequences * sizeof(struct sequence_info_t),
                                                      amy_global.config.ram_caps_synth);
    for (int32_t i = 0; i < max_sequences; ++i) {
        sequences[i].wire = NULL;
        sequences[i].tick = 0;
        sequences[i].period = 0;
    }
    // We are read to go.
    sequencer_recompute();
}

void sequencer_reset() {
    // Remove all events.  No lock here: this is called from play_delta()
    // (RESET_SEQUENCER), which already runs under the amy lock.
    for (int32_t i = 0; i < max_sequences; ++i) {
        if (sequences[i].wire) {
            free(sequences[i].wire);
            sequences[i].wire = NULL;
            sequences[i].tick = 0;
            sequences[i].period = 0;
        }
    }
    highest_tag = -1;
}

void timed_wire_reset() {
    // Drop all pending timed messages (RESET_EVENTS and the overload failsafe).
    amy_grab_lock();
    timed_wire_t *t = timed_wire_head;
    timed_wire_head = NULL;
    amy_release_lock();
    while (t) {
        timed_wire_t *next = t->next;
        if (t->wire) free(t->wire);
        free(t);  // an event entry is co-allocated with its node
        t = next;
    }
}

void sequencer_deinit() {
    sequencer_reset();
    timed_wire_reset();
    if (sequences != NULL) free(sequences);
    sequences = NULL;  // sequencer_check_and_fill guards on this
    max_sequences = 0;
}

void sequencer_debug() {
    fprintf(stderr, "sequencer: max_sequences %" PRIi32" highest_tag %" PRIi32 "\n", max_sequences, highest_tag);
    for (int32_t tag = 0; tag <= highest_tag; ++tag) {
        if (sequences[tag].wire) {
            fprintf(stderr, "sequence tag %" PRIi32" tick %" PRIu32 " period %"PRIu32 " wire \"%s\"\n",
                    tag, sequences[tag].tick, sequences[tag].period, sequences[tag].wire);
        }
    }
    for (timed_wire_t *t = timed_wire_head; t != NULL; t = t->next) {
        fprintf(stderr, "timed wire time %" PRIu32 " %s\"%s\"\n", t->time,
                t->wire ? "wire " : "event ", t->wire ? t->wire : "");
    }
}

void sequencer_recompute() {
    // 60000000 us/min / (bpm * ticks per beat); keep it single-precision -
    // unsuffixed double literals pull in software double emulation on 32-bit.
    amy_global.us_per_tick = (uint32_t) (60000000.0f / (amy_global.tempo * (float)AMY_SEQUENCER_PPQ));
    amy_global.next_amy_tick_us = (amy_sysclock64() * 1000ULL) + (uint64_t)amy_global.us_per_tick;
}

// Store a wire message in the sequencer.  Takes ownership of wire (malloc'd).
// If tick and period are both zero, the tag's entry is cleared.
uint8_t sequencer_add_wire(uint32_t tick, uint32_t period, uint32_t tag, char *wire) {
    if (sequences == NULL) {  // sequencer_init hasn't run
        free(wire);
        return 0;
    }
    if (tag >= (uint32_t)max_sequences) {
        fprintf(stderr, "sequencer tag %" PRIu32" (with tick %" PRIu32", period %" PRIu32") is greater than or eq max_sequences %" PRIi32"\n",
                tag, tick, period, max_sequences);
        free(wire);
        return 0;
    }
    amy_grab_lock();
    // Release any existing message for this tag, even if we're just going to rewrite it.
    if (sequences[tag].wire) free(sequences[tag].wire);
    sequences[tag].wire = NULL;
    sequences[tag].tick = 0;
    sequences[tag].period = 0;
    if ((tick == 0 && period == 0) ||  // Non-schedulable event: just clear the tag.
        (tick != 0 && period == 0 && tick <= amy_global.sequencer_tick_count)) {  // don't schedule things in the past.
        amy_release_lock();
        free(wire);
        return 0;
    }
    sequences[tag].tick = tick;
    sequences[tag].period = period;
    sequences[tag].wire = wire;
    if ((int32_t)tag > highest_tag) highest_tag = tag;  // To limit scanning through tags.
    amy_release_lock();
    return 1;
}

static void timed_insert(timed_wire_t *n) {
    amy_grab_lock();
    // Insert in time order; equal times keep arrival order (insert after).
    // Wrap-relative compare, same as the delta queue: see AMY_TIME_GEQ.
    timed_wire_t **pptr = &timed_wire_head;
    while (*pptr && AMY_TIME_GEQ(n->time, (*pptr)->time))
        pptr = &(*pptr)->next;
    n->next = *pptr;
    *pptr = n;
    amy_release_lock();
}

// Store a one-shot wire message to play at an absolute ms time.
// Takes ownership of wire (malloc'd).
void timed_wire_add(uint32_t time, char *wire) {
    timed_wire_t *n = (timed_wire_t *)malloc_caps(sizeof(timed_wire_t), amy_global.config.ram_caps_events);
    if (n == NULL) {
        amy_oom("timed_wire_add");
        free(wire);
        return;
    }
    n->wire = wire;
    n->event = NULL;
    n->midi_len = 0;
    n->time = time;
    timed_insert(n);
}

// Store a copy of an already-parsed event (C API with a future time) to play
// at an absolute ms time.  e->time must already be the final playback time
// (latency included).
void timed_event_add(uint32_t time, amy_event *e) {
    timed_wire_t *n = (timed_wire_t *)malloc_caps(sizeof(timed_wire_t) + sizeof(amy_event),
                                                  amy_global.config.ram_caps_events);
    if (n == NULL) {
        amy_oom("timed_event_add");
        return;
    }
    n->wire = NULL;
    n->event = (amy_event *)(n + 1);
    *n->event = *e;
    n->midi_len = 0;
    n->time = time;
    timed_insert(n);
}

// Store a raw (non-sysex) MIDI message with a future timestamp.
void timed_midi_add(uint32_t time, uint8_t *bytes, uint8_t len) {
    timed_wire_t *n = (timed_wire_t *)malloc_caps(sizeof(timed_wire_t), amy_global.config.ram_caps_events);
    if (n == NULL) {
        amy_oom("timed_midi_add");
        return;
    }
    n->wire = NULL;
    n->event = NULL;
    n->midi_len = len;
    for (uint8_t i = 0; i < len && i < 3; ++i) n->midi_bytes[i] = bytes[i];
    n->time = time;
    timed_insert(n);
}

// Called once per block from amy_execute_deltas(), and independent of the
// sequencer transport: timed messages play whether or not the sequencer runs.
void timed_wire_check_and_fire() {
    if (wire_firing) return;  // nested via a fired message's own parse
    wire_firing = true;
    for (;;) {
        timed_wire_t *n = NULL;
        amy_grab_lock();
        uint32_t now = amy_sysclock();
        if (timed_wire_head && AMY_TIME_GEQ(now, timed_wire_head->time)) {
            n = timed_wire_head;
            timed_wire_head = n->next;
        }
        amy_release_lock();
        if (n == NULL) break;
        if (n->wire != NULL) {
            // Play with the original 't' as the base time so the resulting deltas
            // carry the same times (and ordering) as if they'd been queued at ingest.
            amy_play_message_with_time(n->wire, n->time);
            free(n->wire);
        } else if (n->event != NULL) {
            // Already-parsed event: its time is final, convert to deltas now.
            amy_event_to_deltas_queue(n->event, 0, &amy_global.delta_queue);
        } else {
            // Raw MIDI: run the full handler now, against current synth state.
            // The message is due, so the handler won't re-defer it.
            midi_message_handler_to_queue(n->midi_bytes, n->midi_len, n->time, NULL, NULL);
        }
        free(n);
    }
    wire_firing = false;
}

static void sequencer_process_tick(void) {
    amy_global.sequencer_tick_count++;
    midi_clock_out_tick();  // no-op unless in AMY_MIDI_SYNC_SEND mode
    // Guard nested check-and-fire calls (via a fired message's own parse)
    // while still processing this tick's fires; restore on the way out.
    bool was_firing = wire_firing;
    wire_firing = true;
    // Scan through the tag table looking for matches
    for (int32_t tag = 0; tag <= highest_tag; ++tag) {
        if (sequences[tag].wire != NULL) {
            bool hit = false;
            bool delete = false;
            if(sequences[tag].period != 0) { // period set
                uint32_t offset = amy_global.sequencer_tick_count % sequences[tag].period;
                if (offset == sequences[tag].tick) hit = true;
            } else {
                // Test for absolute tick (no period set)
                if (sequences[tag].tick == amy_global.sequencer_tick_count) { hit = true; delete = true; }
            }
            if(hit) {
                // Take the message out (one-shot) or a copy of it (repeating)
                // under the lock, so an ingest thread rewriting the tag can't
                // free the string while we parse it.
                char *wire = NULL;
                amy_grab_lock();
                if (sequences[tag].wire != NULL) {
                    if (delete) {
                        wire = sequences[tag].wire;
                        sequences[tag].wire = NULL;
                        sequences[tag].tick = 0;
                        sequences[tag].period = 0;
                    } else {
                        size_t len = strlen(sequences[tag].wire);
                        wire = (char *)malloc_caps(len + 1, amy_global.config.ram_caps_events);
                        if (wire != NULL) memcpy(wire, sequences[tag].wire, len + 1);
                        else amy_oom("sequencer fire");
                    }
                }
                amy_release_lock();
                if (wire != NULL) {
                    // Parse and play now; the deltas play back within this block.
                    amy_play_message_with_time(wire, UINT32_MAX /* no base time: play now */);
                    free(wire);
                }
            }
        }
    }
    wire_firing = was_firing;
    if(amy_global.config.amy_external_sequencer_hook != NULL) {
        amy_global.config.amy_external_sequencer_hook(amy_global.sequencer_tick_count);
    }
}

#ifdef __EMSCRIPTEN__
// On the web, ticks are counted in the render loop, which runs in the
// AudioWorklet thread -- EM_ASM there can't reach the page's JS, where
// amy_sequencer_js_hook is defined. The emscripten main loop calls this from
// the browser main thread to replay elapsed ticks to the hook.
void sequencer_check_and_call_js_hook() {
    static uint32_t last_reported_tick = 0;
    uint32_t tick = amy_global.sequencer_tick_count;
    if (tick < last_reported_tick) last_reported_tick = tick;  // sequencer was reset
    // If we're more than a second of ticks behind (e.g. the page was
    // backgrounded and the main loop paused), skip ahead rather than firing a
    // burst of stale hook calls.
    if (amy_global.us_per_tick > 0) {
        uint32_t ticks_per_sec = 1000000 / amy_global.us_per_tick;
        if (tick - last_reported_tick > ticks_per_sec) last_reported_tick = tick - ticks_per_sec;
    }
    while (last_reported_tick < tick) {
        ++last_reported_tick;
        EM_ASM({
            if(typeof amy_sequencer_js_hook === 'function') {
                amy_sequencer_js_hook($0);
            }
        }, last_reported_tick);
    }
}
#endif

void sequencer_midi_start() {
    // MIDI "Start" restarts the sequencer.
    // If external clock was not previously enabled, keep using internal clock
    // so the sequencer advances on its own without needing F8 ticks.
    if (sequencer_external_clock) {
        amy_global.sequencer_tick_count = 0;
    }
    // Reset the tick timer to now so sequencer_check_and_fill doesn't try to
    // catch up all the ticks that elapsed while stopped.
    amy_global.next_amy_tick_us = amy_sysclock64() * 1000ULL;
    sequencer_running = true;
    midi_clock_out_start();  // tell downstream slaves, if we're the clock master
}

void sequencer_midi_stop() {
    sequencer_running = false;
    midi_clock_out_stop();  // tell downstream slaves, if we're the clock master
}

void sequencer_midi_clock_tick() {
    sequencer_external_clock = true;
    if (!sequencer_running) return;
    for (uint8_t i = 0; i < AMY_SEQUENCER_PPQ/MIDI_SEQUENCER_PPQ; ++i) {
        sequencer_process_tick();
    }
}

void sequencer_external_clock_disable() {
    // Leave external-clock mode and hand back to the internal timer. Without
    // this, sequencer_external_clock latches true on the first F8 tick and is
    // never cleared, so the internal sequencer stays dead once an external
    // clock stops -- even after the caller turns external sync back off. Called
    // from amy_external_midi_sync(0) so disabling sync is a real recovery path.
    sequencer_external_clock = false;
    sequencer_running = true;
    // Re-anchor the tick timer to now so sequencer_check_and_fill doesn't try to
    // replay every tick that elapsed while we were on external clock.
    amy_global.next_amy_tick_us = amy_sysclock64() * 1000ULL;
}

// Called once per block from amy_execute_deltas(). Ticks are decided against
// amy_sysclock(), which counts rendered samples, so the sequencer advances on
// AMY time in any rendering context (live, offline, tests).
void sequencer_check_and_fill() {
    if (sequences == NULL) return;  // sequencer_init hasn't run
    if (sequencer_external_clock) return;
    if (wire_firing) return;  // nested via a fired message's own parse
    // When we're the MIDI clock master, realtime clock keeps flowing even while
    // the transport is stopped so slaves stay tempo-locked; otherwise a stopped
    // sequencer has nothing to do.
    if (!sequencer_running && !midi_clock_out_enabled()) return;
    // If we've fallen behind by more than 1 second (e.g. sequencer was stopped
    // and restarted, or a long blocking operation occurred), skip ahead instead
    // of processing hundreds of backed-up ticks at once.
    // next_amy_tick_us is a 64-bit accumulator, so it must be anchored to the
    // 64-bit clock. Seeding it from the 32-bit amy_sysclock() used to kill the
    // sequencer permanently at the 49.7-day rollover: now_us collapsed to ~0
    // while next_amy_tick_us stayed at ~4.3e12, and neither the catch-up guard
    // (which only handles falling behind) nor the tick loop could ever fire.
    uint64_t now_us = amy_sysclock64() * 1000ULL;
    if (now_us > amy_global.next_amy_tick_us + 1000000ULL) {
        amy_global.next_amy_tick_us = now_us;
    }
    // The while is in case the timer fires later than a tick; (on esp this would be due to SPI or wifi ops)
    while(now_us >= amy_global.next_amy_tick_us) {
        if (sequencer_running) sequencer_process_tick();
        else midi_clock_out_tick();  // transport stopped: clock only, no sequence events
        amy_global.next_amy_tick_us = amy_global.next_amy_tick_us + (uint64_t)amy_global.us_per_tick;
    }
}
