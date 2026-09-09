#include "sequencer.h"
#include "amy.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint32_t sequencer_ticks() { return amy_global.sequencer_tick_count; }

// Sequenced ticks events are stored as the raw wire-message string (with its
// leading 'H' command stripped) plus the scheduling metadata needed to play
// it back.  The string is only parsed when the entry comes due.
typedef struct sequence_info_t {
    char *wire;    // Stored wire message; NULL means the slot is free.
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
    // The tag this entry answers to.  This used to be IMPLICIT -- the slot's
    // own index -- because a tag held exactly one event and a second send to
    // the same tag overwrote the first.  Events on a tag CUMULATE now, so one
    // tag can own any number of slots and the index no longer identifies it.
    // Anonymous entries carry their own slot index here: that keeps them
    // sorting after every user tag (they live past max_sequences) and, since
    // sequencer_add_wire() bounds-checks user tags below max_sequences, no
    // user tag can ever name one.
    uint32_t tag;
    // Next OCCUPIED slot, or -1 for the end.  Only meaningful while this
    // entry has a wire -- `wire != NULL` is what "in the list" means, so
    // there is one source of truth and not two to keep in step.
    int32_t next_active;
} sequence_info_t;

// A fixed pool of event slots.  Slots [0, max_sequences) hold TAGGED events
// and slots past that hold anonymous ones; within the tagged region a slot's
// index means nothing but "this one is free or it isn't", because any number
// of slots can carry the same tag.
struct sequence_info_t *sequences = NULL;
// Size of the tagged region, and the exclusive upper bound on a user tag.
// It is still the "number of tags" knob (amy_config.max_sequencer_tags), but
// now it bounds the number of tagged EVENTS as well: cumulating means N tags
// no longer implies N events.  Both are capped by the same number, so the
// footprint is exactly what it was.
int32_t max_sequences = 0;
// Head of the ascending list of occupied slots (user tags and anonymous
// entries alike); -1 when nothing is scheduled.  This replaces `highest_tag`,
// which was a HIGH-WATER MARK: it only ever grew, so one event at a high tag
// made every tick scan that far for the rest of the session, long after that
// sequence was cleared.  The anonymous pool made that the common case, not a
// corner: anonymous entries are allocated round-robin at indices past
// max_sequences, so a burst of ticks= one-shots pinned the mark at the very
// end of the table permanently.  The cost is proportional to what is
// scheduled now.
int32_t first_active = -1;
// Anonymous (no-tag) entries live past the user-addressable tag range, at
// indices [max_sequences .. max_sequences+AMY_ANON_SEQUENCE_SLOTS), so a
// user-supplied tag (bounds-checked against max_sequences) can never reach
// or clobber one. Allocated round-robin; a new anonymous entry silently
// evicts the oldest one once the pool wraps around.
#define AMY_ANON_SEQUENCE_SLOTS 256
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
        sequences[i].tag = 0;
        sequences[i].next_active = -1;
    }
    first_active = -1;
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
            sequences[i].tag = 0;
        }
        sequences[i].next_active = -1;
    }
    first_active = -1;
}

void sequencer_deinit() {
    if (sequences != NULL) {
        sequencer_reset();
        free(sequences);
        sequences = NULL;  // sequencer_check_and_fill guards on this
    }
    max_sequences = 0;
}

void sequencer_debug() {
    int32_t n_active = 0;
    for (int32_t t = first_active; t != -1; t = sequences[t].next_active) ++n_active;
    fprintf(stderr, "sequencer: max_sequences %" PRIi32" active %" PRIi32 "\n", max_sequences, n_active);
    for (int32_t slot = first_active; slot != -1; slot = sequences[slot].next_active) {
        if (sequences[slot].wire) {
            fprintf(stderr, "sequence slot %" PRIi32 " tag %" PRIu32 "%s tick %" PRIu32 " period %"PRIu32 " wire \"%s\"\n",
                    slot, sequences[slot].tag, slot >= max_sequences ? " (anon)" : "",
                    sequences[slot].tick, sequences[slot].period, sequences[slot].wire);
        }
    }
}

/* The occupied slots, threaded through the table as a list ASCENDING BY TAG.
 *
 * Why threaded rather than a list of its own: the table has to stay
 * indexable, because allocating and freeing a slot both reach it directly
 * and want O(1) to do it.  This gets the tick scan down to the number of
 * sequences actually scheduled without giving that up, and without
 * allocating anything the render thread could walk into while it is being
 * freed.
 *
 * WHY SORTED BY TAG, and it is not tidiness: two sequences that hit on the
 * same tick play in the order they are visited, so the order decides which
 * one wins if they touch the same parameter.  That order has always been
 * tag order, and it stays tag order here.  It used to fall out for free --
 * a tag WAS its slot index, so an ascending list of indices was an
 * ascending list of tags.  Now that several slots can share a tag, the sort
 * key has to be the tag itself, and a new entry is spliced in AFTER every
 * entry with the same tag, so events cumulated onto one tag fire in the
 * order they were added.
 *
 * THREAD SAFETY.  Link mutations happen only under the amy lock --
 * sequencer_add_wire() takes it, sequencer_clear_tag() takes it, the tick
 * loop's delete path takes it, and sequencer_reset() is called with it
 * already held -- so writers are serialized.  The tick WALK, though, runs
 * without the lock, which is safe because the links are INDICES INTO A
 * FIXED ARRAY, not pointers:
 *
 *   - publishing a splice is one aligned 32-bit store, so a walker sees
 *     either the old link or the new one, never half of one;
 *   - every link is an in-bounds slot index, so a walker can never leave
 *     the array however stale the link it read.
 *
 * What is NOT free any more is termination.  While the list was sorted by
 * index, every stored link was greater than the slot holding it, so a walk
 * strictly increased and could not cycle.  Sorting by tag breaks that: a
 * tag's slots come from wherever the pool had room, so links run in both
 * directions and a torn read could in principle close a loop.  So the walk
 * counts its steps and gives up after one full table's worth (see
 * sequencer_process_tick).  A stale link can still make a walker skip a
 * sequence or revisit one for a single tick -- that is the same hazard the
 * indexed sweep always had, and it costs one tick of wrong events -- but it
 * can no longer hang the render thread.
 *
 * A list of malloc'd nodes would be a different class of problem entirely --
 * a torn next pointer walks the render thread into freed memory.
 */
static void active_link(int32_t slot)
{
    /* Only ever called on a slot that is out of the list: `wire != NULL` is
     * what "in the list" means, and every path here has just taken a free
     * (wire == NULL) slot or unlinked the one it is reusing. */
    uint32_t tag = sequences[slot].tag;
    int32_t *prev = &first_active;
    /* <= not <: skip past entries that share this tag, so cumulated events
     * on one tag stay in the order they were added. */
    while (*prev != -1 && sequences[*prev].tag <= tag)
        prev = &sequences[*prev].next_active;
    sequences[slot].next_active = *prev;   /* point at the tail we found... */
    *prev = slot;                          /* ...then publish, in one store */
}

static void active_unlink(int32_t slot)
{
    int32_t *prev = &first_active;
    while (*prev != -1 && *prev != slot)
        prev = &sequences[*prev].next_active;
    if (*prev == slot)
        *prev = sequences[slot].next_active;   /* one store, again */
}

// Empty a slot and take it out of the walk.  Caller holds the amy lock.
static void slot_release(int32_t slot)
{
    if (sequences[slot].wire) free(sequences[slot].wire);
    sequences[slot].wire = NULL;
    sequences[slot].tick = 0;
    sequences[slot].period = 0;
    active_unlink(slot);
}

// Lowest free slot in the tagged region, or -1 if the pool is full.  Lowest
// rather than round-robin so a "clear the tag, re-add the pattern" cycle --
// the normal way a sequencer edits itself -- lands on the same slots every
// time and stays reproducible.  Linear, but adds happen at user rate, not
// per tick.  Caller holds the amy lock.
static int32_t alloc_tagged_slot(void)
{
    for (int32_t slot = 0; slot < max_sequences; ++slot)
        if (sequences[slot].wire == NULL) return slot;
    return -1;
}

// Drop EVERY event stored under `tag` -- the only way to take back a whole
// tag now that adding to one accumulates instead of replacing.  Callers on
// the wire and through amy_add_event() reach this as ticks="0,0,<tag>";
// this is also the direct entry point for a C host.
void sequencer_clear_tag(uint32_t tag)
{
    if (sequences == NULL) return;  // sequencer_init hasn't run
    if (tag >= (uint32_t)max_sequences) {
        fprintf(stderr, "sequencer clear tag %" PRIu32 " is greater than or eq max_sequences %" PRIi32 "\n",
                tag, max_sequences);
        return;
    }
    amy_grab_lock();
    for (int32_t slot = 0; slot < max_sequences; ++slot)
        if (sequences[slot].wire != NULL && sequences[slot].tag == tag)
            slot_release(slot);
    amy_release_lock();
}

void sequencer_recompute() {
    // 60000000 us/min / (bpm * ticks per beat); keep it single-precision -
    // unsuffixed double literals pull in software double emulation on 32-bit.
    amy_global.us_per_tick = (uint32_t) (60000000.0f / (amy_global.tempo * (float)AMY_SEQUENCER_PPQ));
    // A wire message can set an absurd tempo; us_per_tick == 0 would make the
    // catch-up loop in sequencer_check_and_fill spin forever.
    if (amy_global.us_per_tick < 50) amy_global.us_per_tick = 50;
    amy_global.next_amy_tick_us = (amy_sysclock64() * 1000ULL) + (uint64_t)amy_global.us_per_tick;
}

// Store a wire message in the sequencer.  Takes ownership of wire (malloc'd).
//
// has_tag false means tag wasn't supplied by the caller (a 1- or 2-value
// ticks= form): the entry is allocated round-robin from the anonymous pool
// instead of the given tag value, so it's stored but not addressable or
// individually cancelable.
//
// has_tag true is the tag-indexed form, and events on a tag ACCUMULATE: each
// send adds another scheduled event under that tag, with its own tick and
// period, rather than replacing whatever was there.  So a whole pattern can
// live on one tag.  Taking one back is the tick==0 && period==0 send --
// ticks="0,0,<tag>" -- which now clears every event on the tag rather than
// the single entry it used to hold.  That is the same spelling callers have
// always used to cancel, and it still means the same thing: this tag now
// holds nothing.  There is no way to drop ONE event from a tag; rebuild the
// tag instead.
//
// A one-off whose tick is already due or overdue is not stored at all -- it
// plays immediately, before returning.  See the comment at that branch.
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
        if (tick == 0 && period == 0) {  // Non-schedulable event: clear the tag.
            sequencer_clear_tag(tag);
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
    }
    if (period == 0 && tick <= amy_global.sequencer_tick_count) {
        // A one-off that is already due or overdue.  Play it NOW rather than
        // dropping it: a caller that reads the tick clock and schedules
        // relative to it always runs a little after the tick it read (every
        // Python path arrives via a deferred callback), and at 48 PPQ any
        // offset under a tick rounds straight back onto the current count.
        // Dropping made that lost race silent -- it is what made the Tulip
        // arpeggiator play nothing at all.  Late by a fraction of a tick beats
        // never, and it restores what the old millisecond time= did with a
        // past-due event.  NB tick==0 is the cancel form, handled above, so it
        // never reaches here.
        //
        // Play outside the lock, exactly as the tick loop does: amy_queue_lock
        // is a plain non-recursive mutex and amy_play_message() re-enters the
        // parser, which can land back in this function.  It leaves anything
        // else on this tag alone -- an overdue one-off is one event, not a
        // statement about the tag.
        amy_play_message(wire);
        free(wire);
        return 1;
    }
    amy_grab_lock();
    int32_t slot;
    if (has_tag) {
        slot = alloc_tagged_slot();
        if (slot < 0) {
            // The pool is full.  It used to be impossible to run out -- one
            // tag, one slot -- but cumulating means a runaway caller can now
            // fill it, so say so rather than silently dropping events.
            amy_release_lock();
            fprintf(stderr, "sequencer full (%" PRIi32 " events), dropping tag %" PRIu32 "\n",
                    max_sequences, tag);
            free(wire);
            return 0;
        }
    } else {
        slot = max_sequences + anon_cursor;
        anon_cursor = (anon_cursor + 1) % AMY_ANON_SEQUENCE_SLOTS;
        // Round-robin: once the pool wraps, a new entry evicts the oldest.
        slot_release(slot);
        tag = (uint32_t)slot;   // sorts after every user tag; see the struct
    }
    sequences[slot].tick = tick;
    sequences[slot].period = period;
    sequences[slot].tag = tag;
    sequences[slot].wire = wire;
    active_link(slot);   // now that it has a message, put it in the walk
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
    // Walk only the slots that have something scheduled.  This used to sweep
    // 0..highest_tag, a mark that never came down.
    //
    // `steps` bounds the walk at one full table.  A well-formed list can't be
    // longer than that, so it never bites in normal operation; it is there
    // because the list is no longer sorted by slot index (it is sorted by
    // tag), so a torn read during a concurrent splice could in principle
    // close a cycle.  See the thread-safety note above active_link().
    int32_t steps = max_sequences + AMY_ANON_SEQUENCE_SLOTS;
    int32_t slot = first_active;
    while (slot != -1 && steps-- > 0) {
        // Read the link BEFORE anything below can unlink this entry.
        int32_t next = sequences[slot].next_active;
        if (sequences[slot].wire != NULL) {
            bool hit = false;
            bool delete = false;
            if(sequences[slot].period != 0) { // period set
                uint32_t offset = amy_global.sequencer_tick_count % sequences[slot].period;
                if (offset == sequences[slot].tick) hit = true;
            } else {
                // Test for absolute tick (no period set).  <= rather than ==:
                // the walk above runs without the lock, and a stale link can
                // make it skip an entry for a single tick (see the thread
                // safety note).  Under ==, a slot skipped on exactly its tick
                // would sit there forever, holding a slot and never playing.
                // <= lets it fire on the next tick instead, matching the
                // play-it-late rule sequencer_add_wire() uses for a one-off
                // that is already due when it arrives.
                if (sequences[slot].tick <= amy_global.sequencer_tick_count) { hit = true; delete = true; }
            }
            if(hit) {
                // Take the message out (one-shot) or a copy of it (repeating)
                // under the lock, so an ingest thread rewriting the slot can't
                // free the string while we parse it.
                char *wire = NULL;
                amy_grab_lock();
                if (sequences[slot].wire != NULL) {
                    if (delete) {
                        wire = sequences[slot].wire;
                        sequences[slot].wire = NULL;   // slot_release, but we
                        sequences[slot].tick = 0;      // keep the string to
                        sequences[slot].period = 0;    // play it below
                        active_unlink(slot);
                    } else {
                        size_t len = strlen(sequences[slot].wire);
                        wire = (char *)malloc_caps(len + 1, amy_global.config.ram_caps_events);
                        if (wire != NULL) memcpy(wire, sequences[slot].wire, len + 1);
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
        slot = next;
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
