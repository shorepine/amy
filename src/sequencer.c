#include "sequencer.h"
#include "amy.h"

#include <assert.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint32_t sequencer_ticks() { return amy_global.sequencer_tick_count; }

// Sequenced ticks events are stored as the raw wire-message string (with its
// leading 'H' command stripped) plus the scheduling metadata needed to play
// it back.  The string is only parsed when the entry comes due.
typedef struct sequence_info_t {
    char *wire;    // Stored wire message; NULL means the tag is unused.
    //uint32_t tag;  // tag is implicit, it's its index in the table
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
    // Next OCCUPIED slot, or -1 for the end.  Only meaningful while this
    // entry has a wire -- `wire != NULL` is what "in the list" means, so
    // there is one source of truth and not two to keep in step.
    int32_t next_active;
} sequence_info_t;

struct sequence_info_t *sequences = NULL;  // Anonymous direct-schedule slots.
int32_t max_sequences = 0;  // Number of user-addressable tags.
// Head of the ascending list of occupied anonymous slots; -1 when nothing is
// scheduled. This replaces `highest_tag`,
// which was a HIGH-WATER MARK: it only ever grew, so one event at a high tag
// made every tick scan that far for the rest of the session, long after that
// sequence was cleared.  The anonymous pool made that the common case, not a
// corner: anonymous entries are allocated round-robin at indices past
// max_sequences, so a burst of ticks= one-shots pinned the mark at the very
// end of the table permanently.  The cost is proportional to what is
// scheduled now.
int32_t first_active = -1;
// Anonymous (no-tag) entries have their own fixed pool. Allocated round-robin;
// a new anonymous entry silently evicts the oldest once the pool wraps.
#define AMY_ANON_SEQUENCE_SLOTS 256
static int32_t anon_cursor = 0;
static volatile bool sequencer_running = true;
static volatile bool sequencer_external_clock = false;
// Firing a stored message parses it, and parsing (e.g. a patch load) can call
// amy_execute_deltas(), which calls back into the check functions below.  This
// flag makes those nested calls no-ops so a tick is never processed twice.
static volatile bool wire_firing = false;

// Reusable sequences use the same public tag space as legacy root events. A
// definition is copy-on-write: executions retain the exact event list they
// started with while cumulative edits become the definition for future starts.
typedef struct stored_sequence_event_t {
    char *wire;
    uint32_t tick;
    uint32_t period;
} stored_sequence_event_t;

typedef struct stored_sequence_definition_t {
    stored_sequence_event_t *events;
    uint32_t event_count;
    uint32_t last_one_shot_tick;
    bool has_periodic_event;
    uint32_t refs;
    // Zero-reference definitions are linked here by the render path. A
    // non-rendering sequence API call detaches the complete list under the
    // queue lock and performs the variable-time frees after releasing it.
    struct stored_sequence_definition_t *next_retired;
} stored_sequence_definition_t;

typedef struct stored_sequence_slot_t {
    stored_sequence_definition_t *definition;
} stored_sequence_slot_t;

typedef struct stored_sequence_execution_t {
    stored_sequence_definition_t *definition;
    uint32_t tag;
    uint32_t start_tick;
    uint32_t stop_tick;
    uint32_t gate_change_tick;
    uint32_t gate_duration;
    uint32_t gate_end_tick;
    bool occupied;
    bool stop_pending;
    bool gate_change_pending;
    bool gated;
} stored_sequence_execution_t;

static stored_sequence_slot_t *stored_sequences = NULL;
static stored_sequence_execution_t *sequence_executions = NULL;
static uint32_t max_stored_sequence_events = 0;
static uint32_t max_stored_sequence_executions = 0;
static size_t stored_sequence_event_bytes = 0;
static volatile bool stored_sequence_wire_firing = false;
static stored_sequence_definition_t *retired_sequence_definitions = NULL;

#ifdef AMY_SEQUENCE_TESTING
static int32_t stored_sequence_allocations_before_failure = -1;
static void (*stored_sequence_after_pin_hook)(void) = NULL;

void sequencer_test_fail_allocation_after(int32_t successful_allocations) {
    stored_sequence_allocations_before_failure = successful_allocations;
}

void sequencer_test_set_after_pin_hook(void (*hook)(void)) {
    stored_sequence_after_pin_hook = hook;
}
#endif

static void *stored_sequence_allocate(uint32_t size, uint32_t caps) {
#ifdef AMY_SEQUENCE_TESTING
    if (stored_sequence_allocations_before_failure == 0) return NULL;
    if (stored_sequence_allocations_before_failure > 0)
        stored_sequence_allocations_before_failure--;
#endif
    return malloc_caps(size, caps);
}

static bool checked_array_size(uint32_t count, size_t element_size,
                               size_t *bytes) {
    if (count > SIZE_MAX / element_size) return false;
    *bytes = (size_t)count * element_size;
    return true;
}

static void stored_sequence_definition_destroy(
        stored_sequence_definition_t *definition) {
    if (definition == NULL) return;
    for (uint32_t i = 0; i < definition->event_count; ++i)
        if (definition->events[i].wire != NULL) free(definition->events[i].wire);
    free(definition->events);
    free(definition);
}

// References are changed only while amy_queue_lock is held. Return the object
// which reached zero so the caller can either retire it (render path) or free
// it after dropping the lock (control path).
static stored_sequence_definition_t *stored_sequence_definition_unref_locked(
        stored_sequence_definition_t *definition) {
    if (definition == NULL) return NULL;
    assert(definition->refs != 0);
    definition->refs--;
    return definition->refs == 0 ? definition : NULL;
}

static void stored_sequence_definition_retire_locked(
        stored_sequence_definition_t *definition) {
    stored_sequence_definition_t *retired =
        stored_sequence_definition_unref_locked(definition);
    if (retired == NULL) return;
    retired->next_retired = retired_sequence_definitions;
    retired_sequence_definitions = retired;
}

static void stored_sequence_definition_destroy_list(
        stored_sequence_definition_t *definition) {
    while (definition != NULL) {
        stored_sequence_definition_t *next = definition->next_retired;
        stored_sequence_definition_destroy(definition);
        definition = next;
    }
}

// The public wire boundary calls this unconditionally after parsing. Sequence
// entry points also use it opportunistically, except while a render-fired wire
// is active. Keeping the actual destruction here makes that distinction
// explicit instead of trying to infer the caller from concurrent global state.
void sequencer_reclaim_retired(void) {
    amy_grab_lock();
    stored_sequence_definition_t *retired = retired_sequence_definitions;
    retired_sequence_definitions = NULL;
    amy_release_lock();
    stored_sequence_definition_destroy_list(retired);
}

static void stored_sequence_reclaim_retired(void) {
    if (wire_firing || stored_sequence_wire_firing) return;
    sequencer_reclaim_retired();
}

static stored_sequence_definition_t *stored_sequence_definition_new(void) {
    stored_sequence_definition_t *definition =
        (stored_sequence_definition_t *)stored_sequence_allocate(
            sizeof(stored_sequence_definition_t),
            amy_global.config.ram_caps_synth);
    if (definition == NULL) return NULL;
    definition->events = (stored_sequence_event_t *)stored_sequence_allocate(
        stored_sequence_event_bytes, amy_global.config.ram_caps_synth);
    if (definition->events == NULL) {
        free(definition);
        return NULL;
    }
    memset(definition->events, 0, stored_sequence_event_bytes);
    definition->event_count = 0;
    definition->last_one_shot_tick = 0;
    definition->has_periodic_event = false;
    definition->refs = 1;
    definition->next_retired = NULL;
    return definition;
}

static char *stored_sequence_wire_copy(const char *wire) {
    size_t len = strlen(wire);
    char *copy = (char *)stored_sequence_allocate(
        len + 1, amy_global.config.ram_caps_events);
    if (copy != NULL) memcpy(copy, wire, len + 1);
    return copy;
}

static stored_sequence_definition_t *stored_sequence_definition_clone(
        const stored_sequence_definition_t *source) {
    stored_sequence_definition_t *copy = stored_sequence_definition_new();
    if (copy == NULL) return NULL;
    if (source == NULL) return copy;
    copy->event_count = source->event_count;
    copy->last_one_shot_tick = source->last_one_shot_tick;
    copy->has_periodic_event = source->has_periodic_event;
    for (uint32_t i = 0; i < source->event_count; ++i) {
        const stored_sequence_event_t *from = &source->events[i];
        copy->events[i].wire = stored_sequence_wire_copy(from->wire);
        if (copy->events[i].wire == NULL) {
            stored_sequence_definition_destroy(copy);
            return NULL;
        }
        copy->events[i].tick = from->tick;
        copy->events[i].period = from->period;
    }
    return copy;
}

static void stored_sequence_execution_release_deferred(
        stored_sequence_execution_t *execution) {
    if (!execution->occupied) return;
    stored_sequence_definition_t *definition = execution->definition;
    memset(execution, 0, sizeof(*execution));
    stored_sequence_definition_retire_locked(definition);
}

static void stored_sequence_executions_reset(void) {
    if (sequence_executions == NULL) return;
    for (uint32_t i = 0; i < max_stored_sequence_executions; ++i)
        stored_sequence_execution_release_deferred(&sequence_executions[i]);
}

static void stored_sequences_clear_definitions(void) {
    if (stored_sequences == NULL) return;
    for (int32_t i = 0; i < max_sequences; ++i) {
        stored_sequence_definition_retire_locked(
            stored_sequences[i].definition);
        stored_sequences[i].definition = NULL;
    }
}

static void stored_sequences_deinit(void) {
    stored_sequence_executions_reset();
    stored_sequences_clear_definitions();
    if (stored_sequences != NULL) {
        free(stored_sequences);
        stored_sequences = NULL;
    }
    if (sequence_executions != NULL) {
        free(sequence_executions);
        sequence_executions = NULL;
    }
    max_stored_sequence_events = 0;
    max_stored_sequence_executions = 0;
    stored_sequence_event_bytes = 0;
    stored_sequence_definition_t *retired = retired_sequence_definitions;
    retired_sequence_definitions = NULL;
    stored_sequence_definition_destroy_list(retired);
}

static void stored_sequences_init(uint32_t events, uint32_t executions) {
    max_stored_sequence_events = events;
    max_stored_sequence_executions = executions;
    stored_sequence_wire_firing = false;
    if (max_sequences == 0 || events == 0 || executions == 0) return;

    size_t slot_bytes = 0;
    size_t execution_bytes = 0;
    if (!checked_array_size((uint32_t)max_sequences,
                            sizeof(stored_sequence_slot_t), &slot_bytes)
        || !checked_array_size(events, sizeof(stored_sequence_event_t),
                               &stored_sequence_event_bytes)
        || !checked_array_size(executions,
                               sizeof(stored_sequence_execution_t),
                               &execution_bytes)) {
        fprintf(stderr,
                "stored sequence configuration exceeds addressable memory: "
                "tags=%" PRIi32 ", events=%" PRIu32
                ", executions=%" PRIu32 "\n",
                max_sequences, events, executions);
        stored_sequences_deinit();
        return;
    }
    stored_sequences = (stored_sequence_slot_t *)malloc_caps(
        slot_bytes, amy_global.config.ram_caps_synth);
    if (stored_sequences != NULL)
        memset(stored_sequences, 0, slot_bytes);
    sequence_executions = (stored_sequence_execution_t *)malloc_caps(
        execution_bytes, amy_global.config.ram_caps_synth);
    if (sequence_executions != NULL)
        memset(sequence_executions, 0, execution_bytes);
    if (stored_sequences == NULL || sequence_executions == NULL) {
        amy_oom("stored sequences");
        stored_sequences_deinit();
        return;
    }
}

void sequencer_init(int max_sequencer_tags, uint32_t sequence_events,
                    uint32_t sequence_execution_count) {
    // These are statics, so a stop/start of AMY within one process needs them
    // put back to their boot state (internal clock, running).
    sequencer_running = true;
    sequencer_external_clock = false;
    wire_firing = false;
    anon_cursor = 0;
    max_sequences = max_sequencer_tags;
    sequences = (struct sequence_info_t *)malloc_caps(AMY_ANON_SEQUENCE_SLOTS * sizeof(struct sequence_info_t),
                                                      amy_global.config.ram_caps_synth);
    for (int32_t i = 0; i < AMY_ANON_SEQUENCE_SLOTS; ++i) {
        sequences[i].wire = NULL;
        sequences[i].tick = 0;
        sequences[i].period = 0;
        sequences[i].next_active = -1;
    }
    first_active = -1;
    stored_sequences_init(sequence_events, sequence_execution_count);
    // We are read to go.
    sequencer_recompute();
}

void sequencer_reset() {
    // Remove all events (tagged and anonymous).  No lock here: this is called
    // from play_delta() (RESET_SEQUENCER), which already runs under the amy lock.
    for (int32_t i = 0; i < AMY_ANON_SEQUENCE_SLOTS; ++i) {
        if (sequences[i].wire) {
            free(sequences[i].wire);
            sequences[i].wire = NULL;
            sequences[i].tick = 0;
            sequences[i].period = 0;
        }
        sequences[i].next_active = -1;
    }
    first_active = -1;
    stored_sequence_executions_reset();
    stored_sequences_clear_definitions();
}

void sequencer_deinit() {
    if (sequences != NULL) {
        sequencer_reset();
        free(sequences);
        sequences = NULL;  // sequencer_check_and_fill guards on this
    }
    max_sequences = 0;
    stored_sequences_deinit();
}

void sequencer_sequence_reset_timebase() {
    // Absolute activation/control ticks cannot be meaningfully rebased across
    // a timebase reset. Stored definitions remain available for relaunch.
    stored_sequence_executions_reset();
}

void sequencer_debug() {
    int32_t n_active = 0;
    for (int32_t t = first_active; t != -1; t = sequences[t].next_active) ++n_active;
    fprintf(stderr, "sequencer: max_sequences %" PRIi32" active %" PRIi32 "\n", max_sequences, n_active);
    for (int32_t tag = first_active; tag != -1; tag = sequences[tag].next_active) {
        if (sequences[tag].wire) {
            fprintf(stderr, "anonymous sequence slot %" PRIi32 " tick %" PRIu32
                    " period %" PRIu32 " wire \"%s\"\n",
                    tag, sequences[tag].tick, sequences[tag].period,
                    sequences[tag].wire);
        }
    }
}

/* The occupied slots, threaded through the table as an ASCENDING list.
 *
 * Why threaded rather than a list of its own: the table has to stay
 * indexable, because add and clear both reach a tag directly and want O(1)
 * to do it.  This gets the tick scan down to the number of sequences
 * actually scheduled without giving that up, and without allocating
 * anything the render thread could walk into while it is being freed.
 *
 * WHY ASCENDING, and it is not tidiness: two sequences that hit on the same
 * tick play in the order they are visited, so the order decides which one
 * wins if they touch the same parameter.  That order was slot order when
 * this was an indexed sweep, and keeping the list sorted keeps it slot
 * order.  An insertion-ordered list would make a pattern sound different
 * after an edit.
 *
 * THREAD SAFETY.  Link mutations happen only under the amy lock --
 * sequencer_add_wire() takes it, the tick loop's delete path takes it, and
 * sequencer_reset() is called with it already held -- so writers are
 * serialized.  The tick WALK, though, runs without the lock, which is safe
 * because the links are INDICES INTO A FIXED ARRAY, not pointers:
 *
 *   - publishing a splice is one aligned 32-bit store, so a walker sees
 *     either the old link or the new one, never half of one;
 *   - every stored link is greater than the slot holding it, so walking
 *     strictly increases the index.  A stale link can make a walker skip a
 *     sequence or revisit one for a single tick; it cannot form a cycle,
 *     cannot hang, and cannot leave the array.
 *
 * So the worst a race costs is one tick's events being wrong, which is the
 * same class of hazard the indexed sweep already had.  A list of malloc'd
 * nodes would be a different class entirely -- a torn next pointer walks
 * the render thread into freed memory.
 */
static void active_link(int32_t tag)
{
    int32_t *prev = &first_active;
    while (*prev != -1 && *prev < tag)
        prev = &sequences[*prev].next_active;
    if (*prev == tag)
        return;                       /* already in */
    sequences[tag].next_active = *prev;   /* point at the tail we found... */
    *prev = tag;                          /* ...then publish, in one store */
}

static void active_unlink(int32_t tag)
{
    int32_t *prev = &first_active;
    while (*prev != -1 && *prev != tag)
        prev = &sequences[*prev].next_active;
    if (*prev == tag)
        *prev = sequences[tag].next_active;   /* one store, again */
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
// ticks= form): the entry is allocated round-robin from the anonymous pool, so
// it is stored but not addressable or individually cancelable. has_tag true
// appends to the reusable definition at that tag; an empty tick-zero message
// resets the definition.
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
        // Tagged ticks are the events of the reusable sequence identified by
        // that tag. Repeating a tag therefore accumulates events, matching
        // the way repeated synth= messages build one synth. The historical
        // empty H0,0,tag form remains a convenient spelling for per-tag reset;
        // with a payload, tick zero is an ordinary (and essential) local
        // one-shot event.
        if (tick == 0 && period == 0
            && (wire == NULL || wire[0] == '\0' || wire[0] == 'Z')) {
            free(wire);
            return sequencer_sequence_reset(tag);
        }
        return sequencer_sequence_add_wire(tag, tick, period, wire);
    } else {
        // Anonymous: tick==0 && period==0 has nothing to cancel (no tag was
        // given), so just drop it rather than allocating a slot for a no-op.
        if (tick == 0 && period == 0) {
            free(wire);
            return 0;
        }
        tag = (uint32_t)anon_cursor;
        anon_cursor = (anon_cursor + 1) % AMY_ANON_SEQUENCE_SLOTS;
    }
    amy_grab_lock();
    // Reuse the selected anonymous slot, evicting its previous message.
    if (sequences[tag].wire) free(sequences[tag].wire);
    sequences[tag].wire = NULL;
    sequences[tag].tick = 0;
    sequences[tag].period = 0;
    active_unlink(tag);   // out of the list while it has nothing in it
    if (tick == 0 && period == 0) {  // Non-schedulable event: just clear the tag.
        amy_release_lock();
        free(wire);
        return 0;
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
        // Play outside the lock and free after, exactly as the tick loop does:
        // amy_queue_lock is a plain non-recursive mutex and amy_play_message()
        // re-enters the parser, which can land back in this function.
        amy_release_lock();
        amy_play_message(wire);
        free(wire);
        return 1;
    }
    sequences[tag].tick = tick;
    sequences[tag].period = period;
    sequences[tag].wire = wire;
    active_link(tag);   // ...and back in, now that it has a message again
    amy_release_lock();
    return 1;
}

static stored_sequence_slot_t *stored_sequence_slot(uint32_t tag) {
    if (stored_sequences == NULL || tag >= (uint32_t)max_sequences) return NULL;
    return &stored_sequences[tag];
}

static void stored_sequence_definition_append_owned(
        stored_sequence_definition_t *definition, uint32_t tick,
        uint32_t period, char *wire) {
    stored_sequence_event_t *event =
        &definition->events[definition->event_count++];
    event->wire = wire;
    event->tick = tick;
    event->period = period;
    if (period != 0) definition->has_periodic_event = true;
    else if (tick > definition->last_one_shot_tick)
        definition->last_one_shot_tick = tick;
}

// A candidate owns the incoming wire in its final event. If publication loses
// a race, detach that event before destroying the private candidate so the
// same caller-owned wire can be retried against the newly published version.
static void stored_sequence_candidate_discard(
        stored_sequence_definition_t *candidate, char *wire) {
    if (candidate != NULL && candidate->event_count != 0) {
        stored_sequence_event_t *event =
            &candidate->events[candidate->event_count - 1];
        if (event->wire == wire) {
            event->wire = NULL;
            candidate->event_count--;
        }
    }
    stored_sequence_definition_destroy(candidate);
}

uint8_t sequencer_sequence_add_wire(uint32_t tag, uint32_t tick,
                                    uint32_t period, char *wire) {
    stored_sequence_slot_t *slot = stored_sequence_slot(tag);
    if (slot == NULL) {
        if (stored_sequences == NULL)
            fprintf(stderr, "cannot append event to sequence %" PRIu32
                    ": stored sequences are disabled\n", tag);
        else
            fprintf(stderr, "cannot append event: sequence tag %" PRIu32
                    " is outside the configured range [0, %" PRIi32 "]\n",
                    tag, max_sequences - 1);
        free(wire);
        return 0;
    }
    if (wire == NULL || wire[0] == '\0' || wire[0] == 'Z') {
        fprintf(stderr, "cannot append event to sequence %" PRIu32
                ": event payload is empty\n", tag);
        free(wire);
        return 0;
    }
    if (wire[0] == 'H' && wire[1] != 'C') {
        fprintf(stderr, "cannot append event to sequence %" PRIu32
                ": only H sequence-control payloads may be nested\n", tag);
        free(wire);
        return 0;
    }
    if (period != 0 && tick >= period) {
        fprintf(stderr, "cannot append event to sequence %" PRIu32
                ": tick %" PRIu32 " must be below period %" PRIu32 "\n",
                tag, tick, period);
        free(wire);
        return 0;
    }

    stored_sequence_reclaim_retired();
#ifdef AMY_SEQUENCE_TESTING
    bool test_pin_hook_called = false;
#endif
    for (;;) {
        amy_grab_lock();
        stored_sequence_definition_t *source = slot->definition;
        if (source != NULL
            && source->event_count >= max_stored_sequence_events) {
            fprintf(stderr, "cannot append event to sequence %" PRIu32
                    ": configured limit of %" PRIu32 " events is full\n",
                    tag, max_stored_sequence_events);
            amy_release_lock();
            free(wire);
            return 0;
        }

        // No execution or other writer can observe a refs==1 definition, so
        // appending the already-allocated incoming wire is a bounded mutation.
        // This keeps bulk preload O(n) instead of cloning on every event.
        if (source != NULL && source->refs == 1) {
            stored_sequence_definition_append_owned(source, tick, period,
                                                     wire);
            amy_release_lock();
            stored_sequence_reclaim_retired();
            return 1;
        }

        // Pin a shared source before leaving the lock. From this point it is
        // immutable, so allocation and all copying can happen without holding
        // up the render thread.
        if (source != NULL) source->refs++;
        amy_release_lock();

#ifdef AMY_SEQUENCE_TESTING
        // Tests use this one-shot rendezvous to make two writers clone the
        // same pinned generation. It is absent from production builds.
        if (!test_pin_hook_called && stored_sequence_after_pin_hook != NULL) {
            test_pin_hook_called = true;
            stored_sequence_after_pin_hook();
        }
#endif

        stored_sequence_definition_t *candidate = source == NULL
            ? stored_sequence_definition_new()
            : stored_sequence_definition_clone(source);
        if (candidate == NULL) {
            stored_sequence_definition_t *dead = NULL;
            if (source != NULL) {
                amy_grab_lock();
                dead = stored_sequence_definition_unref_locked(source);
                amy_release_lock();
            }
            stored_sequence_definition_destroy(dead);
            amy_oom("stored sequence edit: out of memory\n");
            free(wire);
            return 0;
        }
        stored_sequence_definition_append_owned(candidate, tick, period, wire);

        amy_grab_lock();
        if (slot->definition == source) {
            slot->definition = candidate;
            stored_sequence_definition_t *dead = NULL;
            if (source != NULL) {
                // Drop the old slot ownership and our temporary writer pin.
                dead = stored_sequence_definition_unref_locked(source);
                stored_sequence_definition_t *after_pin =
                    stored_sequence_definition_unref_locked(source);
                if (after_pin != NULL) dead = after_pin;
            }
            amy_release_lock();
            stored_sequence_definition_destroy(dead);
            stored_sequence_reclaim_retired();
            return 1;
        }

        // Another writer published first. Keep the caller's wire, release our
        // source pin, discard the private candidate outside the lock, and retry
        // against the new cumulative definition.
        stored_sequence_definition_t *dead = source == NULL ? NULL
            : stored_sequence_definition_unref_locked(source);
        amy_release_lock();
        stored_sequence_candidate_discard(candidate, wire);
        stored_sequence_definition_destroy(dead);
    }
}

uint8_t sequencer_sequence_reset(uint32_t tag) {
    stored_sequence_slot_t *slot = stored_sequence_slot(tag);
    if (slot == NULL) {
        if (stored_sequences == NULL)
            fprintf(stderr, "cannot reset sequence %" PRIu32
                    ": stored sequences are disabled\n", tag);
        else
            fprintf(stderr, "cannot reset sequence: tag %" PRIu32
                    " is outside the configured range [0, %" PRIi32 "]\n",
                    tag, max_sequences - 1);
        return 0;
    }
    if (stored_sequence_wire_firing) {
        fprintf(stderr, "sequence %" PRIu32
                " cannot reset definitions from a stored sequence event\n",
                tag);
        return 0;
    }

    stored_sequence_reclaim_retired();
    amy_grab_lock();
    stored_sequence_definition_t *definition = slot->definition;
    slot->definition = NULL;
    stored_sequence_definition_t *dead = NULL;
    if (wire_firing) stored_sequence_definition_retire_locked(definition);
    else dead = stored_sequence_definition_unref_locked(definition);
    amy_release_lock();
    stored_sequence_definition_destroy(dead);
    stored_sequence_reclaim_retired();
    return 1;
}

static uint32_t sequence_control_tick(uint32_t alignment_period) {
    // A control fired by the root sequencer participates in this tick. A
    // control arriving between ticks begins no earlier than the next tick.
    uint32_t tick = wire_firing ? amy_global.sequencer_tick_count
                                : amy_global.sequencer_tick_count + 1;
    if (alignment_period != 0) {
        uint32_t remainder = tick % alignment_period;
        if (remainder != 0) tick += alignment_period - remainder;
    }
    return tick;
}

uint8_t sequencer_sequence_control(uint32_t tag, uint32_t action,
                                   uint32_t value,
                                   uint32_t alignment_period) {
    stored_sequence_slot_t *slot = stored_sequence_slot(tag);
    if (slot == NULL) {
        if (stored_sequences == NULL)
            fprintf(stderr, "cannot control sequence %" PRIu32
                    ": stored sequences are disabled\n", tag);
        else
            fprintf(stderr, "cannot control sequence %" PRIu32
                    ": valid tags are [0, %" PRIi32 "]\n",
                    tag, max_sequences - 1);
        return 0;
    }

    stored_sequence_reclaim_retired();
    uint8_t result = 0;
    amy_grab_lock();
    if (action == SEQUENCE_CONTROL_START) {
        if (slot->definition == NULL || slot->definition->event_count == 0) {
            fprintf(stderr, "cannot start sequence %" PRIu32
                    ": its definition is empty\n", tag);
        } else {
            uint32_t start_tick = sequence_control_tick(alignment_period);
            stored_sequence_execution_t *available = NULL;
            for (uint32_t i = 0; i < max_stored_sequence_executions; ++i) {
                stored_sequence_execution_t *execution = &sequence_executions[i];
                if (!execution->occupied && available == NULL) available = execution;
            }
            if (available == NULL) {
                fprintf(stderr, "cannot start sequence %" PRIu32
                        ": all %" PRIu32 " execution slots are occupied\n",
                        tag, max_stored_sequence_executions);
            } else {
                memset(available, 0, sizeof(*available));
                available->definition = slot->definition;
                available->definition->refs++;
                available->tag = tag;
                available->start_tick = start_tick;
                available->occupied = true;
                result = 1;
            }
        }
    } else if (action == SEQUENCE_CONTROL_STOP
               || action == SEQUENCE_CONTROL_GATE) {
        uint32_t control_tick = sequence_control_tick(alignment_period);
        for (uint32_t i = 0; i < max_stored_sequence_executions; ++i) {
            stored_sequence_execution_t *execution = &sequence_executions[i];
            if (!execution->occupied || execution->tag != tag)
                continue;
            if (action == SEQUENCE_CONTROL_STOP) {
                execution->stop_tick = control_tick;
                execution->stop_pending = true;
            } else {
                execution->gate_change_tick = control_tick;
                execution->gate_duration = value;
                execution->gate_change_pending = true;
            }
            result = 1;
        }
    } else {
        fprintf(stderr, "cannot control sequence %" PRIu32
                ": action %" PRIu32 " is unknown; valid actions are "
                "stop=0, start=1, gate=2\n", tag, action);
    }
    amy_release_lock();
    stored_sequence_reclaim_retired();
    return result;
}

static bool stored_sequence_event_hits(const stored_sequence_event_t *event,
                                       uint32_t local_tick) {
    return event->period != 0 ? local_tick % event->period == event->tick
                              : local_tick == event->tick;
}

static bool stored_sequence_event_is_control(
        const stored_sequence_event_t *event) {
    return strncmp(event->wire, "HC", 2) == 0;
}

static void sequence_play_wire_now(char *wire) {
    if (wire[0] == 'H') handle_ticks_message(wire);
    else amy_play_message(wire);
}

static void stored_sequence_play_wire(const char *wire) {
    bool previous = stored_sequence_wire_firing;
    stored_sequence_wire_firing = true;
    sequence_play_wire_now((char *)wire);
    stored_sequence_wire_firing = previous;
}

static void stored_sequence_process_pass(uint32_t tick, bool controls) {
    for (uint32_t i = 0; i < max_stored_sequence_executions; ++i) {
        amy_grab_lock();
        stored_sequence_execution_t *execution = &sequence_executions[i];
        if (!execution->occupied || !AMY_TIME_GEQ(tick, execution->start_tick)) {
            amy_release_lock();
            continue;
        }
        uint32_t elapsed = tick - execution->start_tick;
        stored_sequence_definition_t *definition = execution->definition;
        if ((execution->stop_pending && AMY_TIME_GEQ(tick, execution->stop_tick))
            || (!definition->has_periodic_event
                && elapsed > definition->last_one_shot_tick)) {
            stored_sequence_execution_release_deferred(execution);
            amy_release_lock();
            continue;
        }
        if (!controls) {
            if (execution->gate_change_pending
                && AMY_TIME_GEQ(tick, execution->gate_change_tick)) {
                execution->gate_change_pending = false;
                execution->gated = execution->gate_duration != 0;
                execution->gate_end_tick = execution->gate_change_tick
                                         + execution->gate_duration;
            }
            if (execution->gated && AMY_TIME_GEQ(tick, execution->gate_end_tick))
                execution->gated = false;
        }
        bool suppress = !controls && execution->gated;
        definition->refs++;
        amy_release_lock();

        if (!suppress) {
            for (uint32_t event_index = 0;
                 event_index < definition->event_count; ++event_index) {
                stored_sequence_event_t *event =
                    &definition->events[event_index];
                if (stored_sequence_event_is_control(event) == controls
                    && stored_sequence_event_hits(event, elapsed))
                    stored_sequence_play_wire(event->wire);
            }
        }

        amy_grab_lock();
        stored_sequence_definition_retire_locked(definition);
        amy_release_lock();
    }
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
    int32_t tag = first_active;
    while (tag != -1) {
        // Read the link BEFORE anything below can unlink this entry.
        int32_t next = sequences[tag].next_active;
        if (sequences[tag].wire != NULL) {
            bool hit = false;
            bool delete = false;
            if(sequences[tag].period != 0) { // period set
                uint32_t offset = amy_global.sequencer_tick_count % sequences[tag].period;
                if (offset == sequences[tag].tick) hit = true;
            } else {
                // Test for absolute tick (no period set).  <= rather than ==:
                // the walk above runs without the lock, and a stale link can
                // make it skip an entry for a single tick (see the thread
                // safety note).  Under ==, a slot skipped on exactly its tick
                // would sit there forever, holding an anon slot and never
                // playing.  <= lets it fire on the next tick instead, matching
                // the play-it-late rule sequencer_add_wire() uses for a
                // one-off that is already due when it arrives.
                if (sequences[tag].tick <= amy_global.sequencer_tick_count) { hit = true; delete = true; }
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
                        active_unlink(tag);
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
                    sequence_play_wire_now(wire);
                    free(wire);
                }
            }
        }
        tag = next;
    }
    // Nested controls take effect before ordinary stored-sequence events on
    // the same tick. This lets a parent stop a child without one extra onset.
    stored_sequence_process_pass(amy_global.sequencer_tick_count, true);
    stored_sequence_process_pass(amy_global.sequencer_tick_count, false);
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
