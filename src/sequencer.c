#include "sequencer.h"
#include "amy.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint32_t sequencer_ticks() { return amy_global.sequencer_tick_count; }

// Sequenced ticks events are stored as the raw wire-message string (with its
// leading 'H' command stripped) plus the scheduling metadata needed to play
// it back.  The string is only parsed when the entry comes due, so bulk
// ingest of scheduled events is just a scan and a copy -- no event parsing,
// voice allocation or delta expansion up front.
typedef struct sequence_info_t {
    char *wire;    // Stored wire message; NULL means the tag is unused.
    //uint32_t tag;  // tag is implicit, it's its index in the table
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
} sequence_info_t;

struct sequence_info_t *sequences = NULL;  // An array indexed by tag.
int32_t max_sequences = 0;  // Number of user-addressable tags.
int32_t highest_tag = -1;
// Anonymous (no-tag) entries live past the user-addressable tag range, at
// indices [max_sequences .. max_sequences+AMY_ANON_SEQUENCE_SLOTS), so a
// user-supplied tag (bounds-checked against max_sequences) can never reach
// or clobber one. Allocated round-robin; a new anonymous entry silently
// evicts the oldest one once the pool wraps around.
#define AMY_ANON_SEQUENCE_SLOTS 32
static int32_t anon_cursor = 0;
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
    anon_cursor = 0;
    max_sequences = max_sequencer_tags;
    int32_t total_slots = max_sequences + AMY_ANON_SEQUENCE_SLOTS;
    sequences = (struct sequence_info_t *)malloc_caps(total_slots * sizeof(struct sequence_info_t),
                                                      amy_global.config.ram_caps_synth);
    for (int32_t i = 0; i < total_slots; ++i) {
        sequences[i].wire = NULL;
        sequences[i].tick = 0;
        sequences[i].period = 0;
    }
    // We are read to go.
    sequencer_recompute();
}

void sequencer_reset() {
    // Remove all events (tagged and anonymous).  No lock here: this is called
    // from play_delta() (RESET_SEQUENCER), which already runs under the amy lock.
    for (int32_t i = 0; i < max_sequences + AMY_ANON_SEQUENCE_SLOTS; ++i) {
        if (sequences[i].wire) {
            free(sequences[i].wire);
            sequences[i].wire = NULL;
            sequences[i].tick = 0;
            sequences[i].period = 0;
        }
    }
    highest_tag = -1;
}

void sequencer_deinit() {
    sequencer_reset();
    if (sequences != NULL) free(sequences);
    sequences = NULL;  // sequencer_check_and_fill guards on this
    max_sequences = 0;
}

void sequencer_debug() {
    fprintf(stderr, "sequencer: max_sequences %" PRIi32" highest_tag %" PRIi32 "\n", max_sequences, highest_tag);
    for (int32_t tag = 0; tag <= highest_tag; ++tag) {
        if (sequences[tag].wire) {
            fprintf(stderr, "sequence tag %" PRIi32"%s tick %" PRIu32 " period %"PRIu32 " wire \"%s\"\n",
                    tag, tag >= max_sequences ? " (anon)" : "", sequences[tag].tick, sequences[tag].period, sequences[tag].wire);
        }
    }
}

void sequencer_recompute() {
    // 60000000 us/min / (bpm * ticks per beat); keep it single-precision -
    // unsuffixed double literals pull in software double emulation on 32-bit.
    amy_global.us_per_tick = (uint32_t) (60000000.0f / (amy_global.tempo * (float)AMY_SEQUENCER_PPQ));
    amy_global.next_amy_tick_us = (amy_sysclock64() * 1000ULL) + (uint64_t)amy_global.us_per_tick;
}

// Store a wire message in the sequencer.  Takes ownership of wire (malloc'd).
//
// has_tag false means tag wasn't supplied by the caller (a 1- or 2-value
// ticks= form): the entry is allocated round-robin from the anonymous pool
// instead of the given tag value, so it's stored but not addressable or
// individually cancelable. has_tag true is the normal tag-indexed form: tick
// and period both zero clears that tag's entry (the only way to cancel one).
uint8_t sequencer_add_wire(uint32_t tick, uint32_t period, uint32_t tag, bool has_tag, char *wire) {
    if (sequences == NULL) {  // sequencer_init hasn't run
        free(wire);
        return 0;
    }
    if (has_tag) {
        if (tag >= (uint32_t)max_sequences) {
            fprintf(stderr, "sequencer tag %" PRIu32" (with tick %" PRIu32", period %" PRIu32") is greater than or eq max_sequences %" PRIi32"\n",
                    tag, tick, period, max_sequences);
            free(wire);
            return 0;
        }
    } else {
        // Anonymous: tick==0 && period==0 has nothing to cancel (no tag was
        // given), so just drop it rather than allocating a slot for a no-op.
        if (tick == 0 && period == 0) {
            free(wire);
            return 0;
        }
        tag = (uint32_t)(max_sequences + anon_cursor);
        anon_cursor = (anon_cursor + 1) % AMY_ANON_SEQUENCE_SLOTS;
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
                    amy_play_message(wire);
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
