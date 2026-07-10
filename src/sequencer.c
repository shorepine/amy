#include "sequencer.h"
#include "amy.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint32_t sequencer_ticks() { return amy_global.sequencer_tick_count; }

typedef struct sequence_info_t {
    struct delta *deltas;
    //uint32_t tag;  // tag is implicit, it's its index in the table
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
} sequence_info_t;

struct sequence_info_t *sequences = NULL;  // An array indexed by tag.
int32_t max_sequences = 0;
int32_t highest_tag = -1;
static volatile bool sequencer_running = true;
static volatile bool sequencer_external_clock = false;

void sequencer_init(int max_sequencer_tags) {
    // These are statics, so a stop/start of AMY within one process needs them
    // put back to their boot state (internal clock, running).
    sequencer_running = true;
    sequencer_external_clock = false;
    max_sequences = max_sequencer_tags;
    sequences = (struct sequence_info_t *)malloc_caps(max_sequences * sizeof(struct sequence_info_t),
                                                      amy_global.config.ram_caps_synth);
    for (int32_t i = 0; i < max_sequences; ++i) {
        sequences[i].deltas = NULL;
        sequences[i].tick = 0;
        sequences[i].period = 0;
    }
    // We are read to go.
    sequencer_recompute();
}

void sequencer_reset() {
    // Remove all events
    for (int32_t i = 0; i < max_sequences; ++i) {
        if (sequences[i].deltas) {
            delta_release_list(sequences[i].deltas);
            sequences[i].deltas = NULL;
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
    for (int32_t tag = 0; tag < max_sequences; ++tag) {
        if (sequences[tag].deltas) {
            fprintf(stderr, "sequence tag %" PRIu32" tick %" PRIu32 " period %"PRIu32 " num_deltas %"PRIu32 "\n",
                    tag, sequences[tag].tick, sequences[tag].period, delta_list_len(sequences[tag].deltas));
        }
    }
}

void sequencer_recompute() {
    // 60000000 us/min / (bpm * ticks per beat); keep it single-precision -
    // unsuffixed double literals pull in software double emulation on 32-bit.
    amy_global.us_per_tick = (uint32_t) (60000000.0f / (amy_global.tempo * (float)AMY_SEQUENCER_PPQ));
    amy_global.next_amy_tick_us = (((uint64_t)amy_sysclock()) * 1000L) + (uint64_t)amy_global.us_per_tick;
}

static void sequencer_process_tick(void) {
    amy_global.sequencer_tick_count++;
    midi_clock_out_tick();  // no-op unless in AMY_MIDI_SYNC_SEND mode
    // Scan through LL looking for matches
    for (int32_t tag = 0; tag <= highest_tag; ++tag) {
        if (sequences[tag].deltas != NULL) {
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
                struct delta *d = sequences[tag].deltas;
                while(d) {
                    // assume the d->time is 0 and that's good.
                    add_delta_to_queue(d, &amy_global.delta_queue);
                    d = d->next;
                }
                // Delete absolute tick addressed sequence entry if sent
                if(delete) {
                    delta_release_list(sequences[tag].deltas);
                    sequences[tag].deltas = NULL;
                }
            }
        }
    }
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
    amy_global.next_amy_tick_us = (uint64_t)amy_sysclock() * 1000L;
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
    amy_global.next_amy_tick_us = (uint64_t)amy_sysclock() * 1000L;
}

uint8_t sequencer_add_event(amy_event *e) {
    // add this event to the list of sequencer events in the LL.
    // e->sequence is set up.
    // if the tag already exists - if there's tick/period, overwrite, if there's no tick / period, we should remove the entry
    //fprintf(stderr, "sequencer_add_event: e->instrument %d e->note %.0f e->vel %.2f tick %d period %d tag %d\n", e->instrument, e->midi_note, e->velocity, e->sequence[SEQUENCE_TICK], e->sequence[SEQUENCE_PERIOD], e->sequence[SEQUENCE_TAG]);
    int32_t tag = e->sequence[SEQUENCE_TAG];
    if (tag > max_sequences) {
        fprintf(stderr, "sequencer tag %" PRIi32" (with tick %" PRIu32", period %" PRIu32") is greater than or eq max_sequences %" PRIi32"\n",
                tag, e->sequence[SEQUENCE_TICK], e->sequence[SEQUENCE_PERIOD], max_sequences);
        // ignore
        return 0;
    }
    // Release any existing deltas for this tag, even if we're just going to rewrite them.
    delta_release_list(sequences[tag].deltas);
    sequences[tag].deltas = NULL;

    if(e->sequence[SEQUENCE_TICK] == 0 && e->sequence[SEQUENCE_PERIOD] == 0) return 0; // Ignore non-schedulable event.
    if(e->sequence[SEQUENCE_TICK] != 0 && e->sequence[SEQUENCE_PERIOD] == 0 && e->sequence[SEQUENCE_TICK] <= amy_global.sequencer_tick_count) return 0; // don't schedule things in the past.

    // Save the tick & period.
    sequences[tag].tick = e->sequence[SEQUENCE_TICK];
    sequences[tag].period = e->sequence[SEQUENCE_PERIOD];
    // Copy all the deltas for this event to the sequences entry.
    amy_event_to_deltas_queue(e, 0, &sequences[tag].deltas);

    if (tag > highest_tag) highest_tag = tag;  // To limit scanning through tags.

    return 1;
}


// Called once per block from amy_execute_deltas(). Ticks are decided against
// amy_sysclock(), which counts rendered samples, so the sequencer advances on
// AMY time in any rendering context (live, offline, tests).
void sequencer_check_and_fill() {
    if (sequences == NULL) return;  // sequencer_init hasn't run
    if (sequencer_external_clock) return;
    // When we're the MIDI clock master, realtime clock keeps flowing even while
    // the transport is stopped so slaves stay tempo-locked; otherwise a stopped
    // sequencer has nothing to do.
    if (!sequencer_running && !midi_clock_out_enabled()) return;
    // If we've fallen behind by more than 1 second (e.g. sequencer was stopped
    // and restarted, or a long blocking operation occurred), skip ahead instead
    // of processing hundreds of backed-up ticks at once.
    uint64_t now_us = (uint64_t)amy_sysclock() * 1000L;
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
