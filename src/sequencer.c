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
    char *wire;    // Stored wire message; NULL means the tag is unused.
    //uint32_t tag;  // tag is implicit, it's its index in the table
    uint32_t tick; // 0 means not used
    uint32_t period; // 0 means not used
    // Next OCCUPIED slot, or -1 for the end.  Only meaningful while this
    // entry has a wire -- `wire != NULL` is what "in the list" means, so
    // there is one source of truth and not two to keep in step.
    int32_t next_active;
} sequence_info_t;

struct sequence_info_t *sequences = NULL;  // An array indexed by tag.
int32_t max_sequences = 0;  // Number of user-addressable tags.
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

// A group definition is immutable once published. Edits are accumulated in a
// private copy and become visible together through SEQUENCE_CONTROL_PUBLISH.
// Active executions retain the published revision they started with.
typedef struct sequence_group_event_t {
    char *wire;
    uint32_t tick;
    uint32_t period;
} sequence_group_event_t;

typedef struct sequence_group_definition_t {
    sequence_group_event_t *events;
    uint32_t length_ticks;
    uint32_t refs;
} sequence_group_definition_t;

typedef struct sequence_group_slot_t {
    sequence_group_definition_t *published;
    sequence_group_definition_t *staging;
} sequence_group_slot_t;

typedef struct sequence_group_execution_t {
    sequence_group_definition_t *definition;
    uint32_t group;
    uint32_t start_tick;
    uint32_t repeats;
    uint32_t execution_tag;
    uint32_t stop_tick;
    uint32_t gate_change_tick;
    uint32_t gate_duration;
    uint32_t gate_end_tick;
    bool occupied;
    bool has_execution_tag;
    bool stop_pending;
    bool gate_change_pending;
    bool gated;
} sequence_group_execution_t;

static sequence_group_slot_t *sequence_groups = NULL;
static sequence_group_execution_t *group_executions = NULL;
static uint32_t max_sequence_groups = 0;
static uint32_t max_sequence_group_tags = 0;
static uint32_t max_sequence_group_executions = 0;
static size_t sequence_group_event_bytes = 0;
static volatile bool group_wire_firing = false;

static bool checked_array_size(uint32_t count, size_t element_size,
                               size_t *bytes) {
    if (count > SIZE_MAX / element_size) return false;
    *bytes = (size_t)count * element_size;
    return true;
}

static void group_definition_release(sequence_group_definition_t *definition) {
    if (definition == NULL || definition->refs == 0) return;
    definition->refs--;
    if (definition->refs != 0) return;
    for (uint32_t i = 0; i < max_sequence_group_tags; ++i)
        if (definition->events[i].wire != NULL) free(definition->events[i].wire);
    free(definition->events);
    free(definition);
}

static sequence_group_definition_t *group_definition_new(void) {
    sequence_group_definition_t *definition =
        (sequence_group_definition_t *)malloc_caps(sizeof(sequence_group_definition_t),
                                                    amy_global.config.ram_caps_synth);
    if (definition == NULL) return NULL;
    definition->events = (sequence_group_event_t *)malloc_caps(
        sequence_group_event_bytes, amy_global.config.ram_caps_synth);
    if (definition->events == NULL) {
        free(definition);
        return NULL;
    }
    memset(definition->events, 0, sequence_group_event_bytes);
    definition->length_ticks = 0;
    definition->refs = 1;
    return definition;
}

static char *group_wire_copy(const char *wire) {
    size_t len = strlen(wire);
    char *copy = (char *)malloc_caps(len + 1, amy_global.config.ram_caps_events);
    if (copy != NULL) memcpy(copy, wire, len + 1);
    return copy;
}

static sequence_group_definition_t *group_definition_clone(
        const sequence_group_definition_t *source) {
    sequence_group_definition_t *copy = group_definition_new();
    if (copy == NULL) return NULL;
    if (source == NULL) return copy;
    copy->length_ticks = source->length_ticks;
    for (uint32_t i = 0; i < max_sequence_group_tags; ++i) {
        const sequence_group_event_t *from = &source->events[i];
        if (from->wire == NULL) continue;
        copy->events[i].wire = group_wire_copy(from->wire);
        if (copy->events[i].wire == NULL) {
            group_definition_release(copy);
            return NULL;
        }
        copy->events[i].tick = from->tick;
        copy->events[i].period = from->period;
    }
    return copy;
}

static void group_execution_release(sequence_group_execution_t *execution) {
    if (!execution->occupied) return;
    sequence_group_definition_t *definition = execution->definition;
    memset(execution, 0, sizeof(*execution));
    group_definition_release(definition);
}

static void group_executions_reset(void) {
    if (group_executions == NULL) return;
    for (uint32_t i = 0; i < max_sequence_group_executions; ++i)
        group_execution_release(&group_executions[i]);
}

static void sequence_groups_deinit(void) {
    group_executions_reset();
    if (sequence_groups != NULL) {
        for (uint32_t i = 0; i < max_sequence_groups; ++i) {
            group_definition_release(sequence_groups[i].published);
            group_definition_release(sequence_groups[i].staging);
        }
        free(sequence_groups);
        sequence_groups = NULL;
    }
    if (group_executions != NULL) {
        free(group_executions);
        group_executions = NULL;
    }
    max_sequence_groups = 0;
    max_sequence_group_tags = 0;
    max_sequence_group_executions = 0;
    sequence_group_event_bytes = 0;
}

static void sequence_groups_init(uint32_t groups, uint32_t tags,
                                 uint32_t executions) {
    max_sequence_groups = groups;
    max_sequence_group_tags = tags;
    max_sequence_group_executions = executions;
    group_wire_firing = false;
    if (groups == 0 || tags == 0 || executions == 0) return;

    size_t group_bytes = 0;
    size_t execution_bytes = 0;
    if (!checked_array_size(groups, sizeof(sequence_group_slot_t), &group_bytes)
        || !checked_array_size(tags, sizeof(sequence_group_event_t),
                               &sequence_group_event_bytes)
        || !checked_array_size(executions,
                               sizeof(sequence_group_execution_t),
                               &execution_bytes)) {
        fprintf(stderr,
                "sequencer group configuration exceeds addressable memory: "
                "groups=%" PRIu32 ", event_tags=%" PRIu32
                ", executions=%" PRIu32 "\n",
                groups, tags, executions);
        sequence_groups_deinit();
        return;
    }
    sequence_groups = (sequence_group_slot_t *)malloc_caps(
        group_bytes, amy_global.config.ram_caps_synth);
    if (sequence_groups != NULL)
        memset(sequence_groups, 0, group_bytes);
    group_executions = (sequence_group_execution_t *)malloc_caps(
        execution_bytes, amy_global.config.ram_caps_synth);
    if (group_executions != NULL)
        memset(group_executions, 0, execution_bytes);
    if (sequence_groups == NULL || group_executions == NULL) {
        amy_oom("sequencer groups");
        sequence_groups_deinit();
        return;
    }
}

void sequencer_init(int max_sequencer_tags, uint32_t groups,
                    uint32_t group_tags, uint32_t group_execution_count) {
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
        sequences[i].next_active = -1;
    }
    first_active = -1;
    sequence_groups_init(groups, group_tags, group_execution_count);
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
        sequences[i].next_active = -1;
    }
    first_active = -1;
    // Definitions are preloadable state and deliberately survive a transport
    // reset; only their active or quantized executions are discarded.
    group_executions_reset();
}

void sequencer_deinit() {
    if (sequences != NULL) {
        sequencer_reset();
        free(sequences);
        sequences = NULL;  // sequencer_check_and_fill guards on this
    }
    max_sequences = 0;
    sequence_groups_deinit();
}

void sequencer_group_reset_timebase() {
    // Absolute activation/control ticks cannot be meaningfully rebased across
    // a timebase reset. Persistent definitions remain available for relaunch.
    group_executions_reset();
}

void sequencer_debug() {
    int32_t n_active = 0;
    for (int32_t t = first_active; t != -1; t = sequences[t].next_active) ++n_active;
    fprintf(stderr, "sequencer: max_sequences %" PRIi32" active %" PRIi32 "\n", max_sequences, n_active);
    for (int32_t tag = first_active; tag != -1; tag = sequences[tag].next_active) {
        if (sequences[tag].wire) {
            fprintf(stderr, "sequence tag %" PRIi32"%s tick %" PRIu32 " period %"PRIu32 " wire \"%s\"\n",
                    tag, tag >= max_sequences ? " (anon)" : "", sequences[tag].tick, sequences[tag].period, sequences[tag].wire);
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
// ticks= form): the entry is allocated round-robin from the anonymous pool
// instead of the given tag value, so it's stored but not addressable or
// individually cancelable. has_tag true is the normal tag-indexed form: tick
// and period both zero clears that tag's entry (the only way to cancel one).
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

static sequence_group_slot_t *group_slot(uint32_t group) {
    if (sequence_groups == NULL || group == 0 || group > max_sequence_groups)
        return NULL;
    return &sequence_groups[group - 1];
}

uint8_t sequencer_group_add_wire(uint32_t tick, uint32_t period,
                                 uint32_t tag, uint32_t group, char *wire) {
    sequence_group_slot_t *slot = group_slot(group);
    if (slot == NULL) {
        if (sequence_groups == NULL)
            fprintf(stderr, "cannot add event to sequencer group %" PRIu32
                    ": sequencer groups are disabled\n", group);
        else
            fprintf(stderr, "cannot add event: sequencer group %" PRIu32
                    " is outside the configured range [1, %" PRIu32 "]\n",
                    group, max_sequence_groups);
        free(wire);
        return 0;
    }
    if (tag >= max_sequence_group_tags) {
        fprintf(stderr, "cannot add event tag %" PRIu32
                " to sequencer group %" PRIu32
                ": valid event tags are [0, %" PRIu32 "]\n",
                tag, group, max_sequence_group_tags - 1);
        free(wire);
        return 0;
    }
    if (wire == NULL) {
        fprintf(stderr, "cannot add event tag %" PRIu32
                " to sequencer group %" PRIu32 ": wire is NULL\n",
                tag, group);
        return 0;
    }
    if (wire[0] == 'H') {
        fprintf(stderr, "cannot add event tag %" PRIu32
                " to sequencer group %" PRIu32
                ": a grouped event cannot contain another ticks command\n",
                tag, group);
        free(wire);
        return 0;
    }

    amy_grab_lock();
    if (slot->staging == NULL) {
        slot->staging = group_definition_clone(slot->published);
        if (slot->staging == NULL) {
            amy_release_lock();
            amy_oom("sequencer group edit");
            free(wire);
            return 0;
        }
    }
    sequence_group_event_t *event = &slot->staging->events[tag];
    if (event->wire != NULL) free(event->wire);
    event->wire = NULL;
    event->tick = 0;
    event->period = 0;
    if (tick != 0 || period != 0) {
        event->wire = wire;
        event->tick = tick;
        event->period = period;
        wire = NULL;
    }
    amy_release_lock();
    if (wire != NULL) free(wire);
    return 1;
}

static uint32_t group_control_tick(uint32_t quantize) {
    // A control fired by the root sequencer participates in this tick. A
    // control arriving between ticks begins no earlier than the next tick.
    uint32_t tick = wire_firing ? amy_global.sequencer_tick_count
                                : amy_global.sequencer_tick_count + 1;
    if (quantize != 0) {
        uint32_t remainder = tick % quantize;
        if (remainder != 0) tick += quantize - remainder;
    }
    return tick;
}

static bool group_execution_matches(const sequence_group_execution_t *execution,
                                    uint32_t group, uint32_t execution_tag,
                                    bool has_execution_tag) {
    if (!execution->occupied || execution->group != group) return false;
    return !has_execution_tag
        || (execution->has_execution_tag
            && execution->execution_tag == execution_tag);
}

static uint8_t group_publish(sequence_group_slot_t *slot, uint32_t group,
                             uint32_t length) {
    if (length == 0) {
        fprintf(stderr, "cannot publish sequencer group %" PRIu32
                ": length must be greater than zero\n", group);
        return 0;
    }
    if (slot->staging == NULL) {
        slot->staging = group_definition_clone(slot->published);
        if (slot->staging == NULL) {
            amy_oom("sequencer group publish");
            return 0;
        }
    }
    for (uint32_t i = 0; i < max_sequence_group_tags; ++i) {
        sequence_group_event_t *event = &slot->staging->events[i];
        if (event->wire == NULL) continue;
        if (event->tick >= length) {
            fprintf(stderr, "cannot publish sequencer group %" PRIu32
                    ": event tag %" PRIu32 " has tick %" PRIu32
                    ", which must be below group length %" PRIu32 "\n",
                    group, i, event->tick, length);
            return 0;
        }
        if (event->period != 0 && event->tick >= event->period) {
            fprintf(stderr, "cannot publish sequencer group %" PRIu32
                    ": event tag %" PRIu32 " has tick %" PRIu32
                    ", which must be below its period %" PRIu32 "\n",
                    group, i, event->tick, event->period);
            return 0;
        }
    }
    slot->staging->length_ticks = length;
    sequence_group_definition_t *previous = slot->published;
    slot->published = slot->staging;
    slot->staging = NULL;
    group_definition_release(previous);
    return 1;
}

uint8_t sequencer_group_control(uint32_t group, uint32_t action,
                                uint32_t value, uint32_t quantize,
                                uint32_t execution_tag,
                                bool has_execution_tag) {
    sequence_group_slot_t *slot = group_slot(group);
    if (slot == NULL) {
        if (sequence_groups == NULL)
            fprintf(stderr, "cannot control sequencer group %" PRIu32
                    ": sequencer groups are disabled\n", group);
        else
            fprintf(stderr, "cannot control sequencer group %" PRIu32
                    ": valid groups are [1, %" PRIu32 "]\n",
                    group, max_sequence_groups);
        return 0;
    }
    if (group_wire_firing
        && (action == SEQUENCE_CONTROL_START
            || action == SEQUENCE_CONTROL_PUBLISH
            || action == SEQUENCE_CONTROL_CLEAR)) {
        fprintf(stderr, "sequencer group %" PRIu32
                " cannot perform lifecycle action %" PRIu32
                ": grouped events may only stop or gate executions\n",
                group, action);
        return 0;
    }

    uint8_t result = 0;
    amy_grab_lock();
    if (action == SEQUENCE_CONTROL_PUBLISH) {
        result = group_publish(slot, group, value);
    } else if (action == SEQUENCE_CONTROL_CLEAR) {
        group_definition_release(slot->published);
        group_definition_release(slot->staging);
        slot->published = NULL;
        slot->staging = NULL;
        result = 1;
    } else if (action == SEQUENCE_CONTROL_START) {
        if (slot->published == NULL || slot->published->length_ticks == 0) {
            fprintf(stderr, "sequencer group %" PRIu32 " has no published definition\n",
                    group);
        } else {
            uint32_t start_tick = group_control_tick(quantize);
            sequence_group_execution_t *available = NULL;
            for (uint32_t i = 0; i < max_sequence_group_executions; ++i) {
                sequence_group_execution_t *execution = &group_executions[i];
                if (!execution->occupied && available == NULL) available = execution;
            }
            if (available == NULL) {
                fprintf(stderr, "cannot start sequencer group %" PRIu32
                        ": all %" PRIu32 " execution slots are occupied\n",
                        group, max_sequence_group_executions);
            } else {
                if (has_execution_tag) {
                    for (uint32_t i = 0; i < max_sequence_group_executions; ++i) {
                        sequence_group_execution_t *execution = &group_executions[i];
                        if (group_execution_matches(execution, group, execution_tag, true)) {
                            execution->stop_tick = start_tick;
                            execution->stop_pending = true;
                        }
                    }
                }
                memset(available, 0, sizeof(*available));
                available->definition = slot->published;
                available->definition->refs++;
                available->group = group;
                available->start_tick = start_tick;
                available->repeats = value;
                available->execution_tag = execution_tag;
                available->has_execution_tag = has_execution_tag;
                available->occupied = true;
                result = 1;
            }
        }
    } else if (action == SEQUENCE_CONTROL_STOP
               || action == SEQUENCE_CONTROL_GATE) {
        uint32_t control_tick = group_control_tick(quantize);
        for (uint32_t i = 0; i < max_sequence_group_executions; ++i) {
            sequence_group_execution_t *execution = &group_executions[i];
            if (!group_execution_matches(execution, group, execution_tag,
                                         has_execution_tag))
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
        fprintf(stderr, "cannot control sequencer group %" PRIu32
                ": action %" PRIu32 " is unknown; valid actions are [0, 4]\n",
                group, action);
    }
    amy_release_lock();
    return result;
}

static bool group_event_hits(const sequence_group_event_t *event,
                             uint32_t local_tick) {
    if (event->wire == NULL) return false;
    return event->period != 0 ? local_tick % event->period == event->tick
                              : local_tick == event->tick;
}

static bool group_event_is_control(const sequence_group_event_t *event) {
    return event->wire != NULL && strncmp(event->wire, "zQ", 2) == 0;
}

static void group_play_wire(const char *wire) {
    bool previous = group_wire_firing;
    group_wire_firing = true;
    amy_play_message((char *)wire);
    group_wire_firing = previous;
}

static void group_process_pass(uint32_t tick, bool controls) {
    for (uint32_t i = 0; i < max_sequence_group_executions; ++i) {
        amy_grab_lock();
        sequence_group_execution_t *execution = &group_executions[i];
        if (!execution->occupied || !AMY_TIME_GEQ(tick, execution->start_tick)) {
            amy_release_lock();
            continue;
        }
        uint32_t elapsed = tick - execution->start_tick;
        sequence_group_definition_t *definition = execution->definition;
        if ((execution->stop_pending && AMY_TIME_GEQ(tick, execution->stop_tick))
            || (execution->repeats != 0
                && elapsed / definition->length_ticks >= execution->repeats)) {
            group_execution_release(execution);
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
        uint32_t local_tick = elapsed % definition->length_ticks;
        amy_release_lock();

        if (!suppress) {
            for (uint32_t tag = 0; tag < max_sequence_group_tags; ++tag) {
                sequence_group_event_t *event = &definition->events[tag];
                if (group_event_is_control(event) == controls
                    && group_event_hits(event, local_tick))
                    group_play_wire(event->wire);
            }
        }

        amy_grab_lock();
        group_definition_release(definition);
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
                    amy_play_message(wire);
                    free(wire);
                }
            }
        }
        tag = next;
    }
    // Controls embedded in a group are leaf operations (stop/gate only) and
    // take effect before any ordinary group event on the same tick.
    group_process_pass(amy_global.sequencer_tick_count, true);
    group_process_pass(amy_global.sequencer_tick_count, false);
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
