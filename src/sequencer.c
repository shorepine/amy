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

// Nested patterns deliberately reuse sequence_info_t: each definition is a
// small sequencer with the same tick/period/tag semantics as the root table.
// Definitions are immutable after commit.  An instance only adds an origin,
// a finite/looping playback mode; pattern events themselves are always
// ordinary AMY events, so nesting stops at exactly two levels.
typedef struct pattern_definition_t {
    sequence_info_t *events;
    int32_t first_active;
    int32_t anon_cursor;
    uint32_t length_ticks;
    uint32_t refs;
    bool retired;
} pattern_definition_t;

typedef struct pattern_slot_t {
    pattern_definition_t *current;
    pattern_definition_t *staging;
} pattern_slot_t;

typedef struct pattern_instance_t {
    pattern_definition_t *definition;
    uint32_t start_tick;
    uint32_t stop_tick;
    uint32_t mute_tick;
    uint32_t mute_duration;
    uint32_t instance_tag;
    uint8_t mode;
    bool occupied;
    bool muted;
} pattern_instance_t;

static pattern_slot_t *pattern_slots = NULL;
static pattern_instance_t *pattern_instances = NULL;
static int32_t max_pattern_slots = 0;
static int32_t max_pattern_event_tags = 0;
static int32_t max_pattern_players = 0;

static void pattern_definition_free(pattern_definition_t *definition);
static void pattern_instances_reset(void);
static void pattern_process_tick(uint32_t tick);

void sequencer_init(int max_sequencer_tags, uint32_t max_patterns,
                    uint32_t max_pattern_tags,
                    uint32_t max_pattern_instances) {
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

    // malloc_caps takes a uint32_t byte count and the active-list indices use
    // -1 as their sentinel. Refuse impossible configurations before either
    // conversion can wrap into a small allocation followed by a large write.
    max_pattern_slots = max_patterns <= INT32_MAX
        && max_patterns <= UINT32_MAX / sizeof(pattern_slot_t)
        ? (int32_t)max_patterns : 0;
    max_pattern_event_tags = max_pattern_tags <= INT32_MAX - AMY_ANON_SEQUENCE_SLOTS
        && max_pattern_tags + AMY_ANON_SEQUENCE_SLOTS
               <= UINT32_MAX / sizeof(sequence_info_t)
        ? (int32_t)max_pattern_tags : 0;
    max_pattern_players = max_pattern_instances <= INT32_MAX
        && max_pattern_instances <= UINT32_MAX / sizeof(pattern_instance_t)
        ? (int32_t)max_pattern_instances : 0;
    if (max_pattern_slots == 0 && max_patterns != 0)
        fprintf(stderr, "max_patterns is too large, nested patterns disabled\n");
    if (max_pattern_event_tags == 0 && max_pattern_tags != 0)
        fprintf(stderr, "max_pattern_tags is too large, nested patterns disabled\n");
    if (max_pattern_players == 0 && max_pattern_instances != 0)
        fprintf(stderr, "max_pattern_instances is too large, nested patterns disabled\n");
    if (max_pattern_slots > 0) {
        pattern_slots = (pattern_slot_t *)malloc_caps(
            (uint32_t)max_pattern_slots * sizeof(pattern_slot_t),
            amy_global.config.ram_caps_synth);
        if (pattern_slots == NULL) {
            amy_oom("pattern slots");
            max_pattern_slots = 0;
        } else {
            bzero(pattern_slots,
                  (size_t)max_pattern_slots * sizeof(pattern_slot_t));
        }
    }
    if (max_pattern_players > 0) {
        pattern_instances = (pattern_instance_t *)malloc_caps(
            (uint32_t)max_pattern_players * sizeof(pattern_instance_t),
            amy_global.config.ram_caps_synth);
        if (pattern_instances == NULL) {
            amy_oom("pattern instances");
            max_pattern_players = 0;
        } else {
            bzero(pattern_instances,
                  (size_t)max_pattern_players * sizeof(pattern_instance_t));
        }
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
        sequences[i].next_active = -1;
    }
    first_active = -1;
    // Stored definitions are analogous to stored patches and survive a
    // sequencer reset.  Running/pending instances are transport state and do
    // not: RESET_SEQUENCER must silence every sequencer level.
    pattern_instances_reset();
}

void sequencer_deinit() {
    if (sequences != NULL) {
        sequencer_reset();
        free(sequences);
        sequences = NULL;  // sequencer_check_and_fill guards on this
    }
    max_sequences = 0;
    pattern_instances_reset();
    if (pattern_slots != NULL) {
        for (int32_t i = 0; i < max_pattern_slots; ++i) {
            pattern_definition_free(pattern_slots[i].staging);
            pattern_definition_free(pattern_slots[i].current);
        }
        free(pattern_slots);
        pattern_slots = NULL;
    }
    if (pattern_instances != NULL) {
        free(pattern_instances);
        pattern_instances = NULL;
    }
    max_pattern_slots = 0;
    max_pattern_event_tags = 0;
    max_pattern_players = 0;
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

static int32_t pattern_total_slots(void) {
    return max_pattern_event_tags + AMY_ANON_SEQUENCE_SLOTS;
}

static pattern_definition_t *pattern_definition_new(uint32_t length_ticks) {
    if (length_ticks == 0 || max_pattern_event_tags <= 0) return NULL;
    int32_t total_slots = pattern_total_slots();
    if (total_slots <= 0) return NULL;

    pattern_definition_t *definition =
        (pattern_definition_t *)malloc_caps(
            sizeof(pattern_definition_t), amy_global.config.ram_caps_events);
    if (definition == NULL) {
        amy_oom("pattern definition");
        return NULL;
    }
    bzero(definition, sizeof(pattern_definition_t));
    definition->events = (sequence_info_t *)malloc_caps(
        (uint32_t)total_slots * sizeof(sequence_info_t),
        amy_global.config.ram_caps_events);
    if (definition->events == NULL) {
        amy_oom("pattern events");
        free(definition);
        return NULL;
    }
    for (int32_t i = 0; i < total_slots; ++i) {
        definition->events[i].wire = NULL;
        definition->events[i].tick = 0;
        definition->events[i].period = 0;
        definition->events[i].next_active = -1;
    }
    definition->first_active = -1;
    definition->anon_cursor = 0;
    definition->length_ticks = length_ticks;
    return definition;
}

static void pattern_definition_free(pattern_definition_t *definition) {
    if (definition == NULL) return;
    if (definition->events != NULL) {
        int32_t total_slots = pattern_total_slots();
        for (int32_t i = 0; i < total_slots; ++i) {
            if (definition->events[i].wire != NULL)
                free(definition->events[i].wire);
        }
        free(definition->events);
    }
    free(definition);
}

static void pattern_active_link(pattern_definition_t *definition,
                                int32_t tag) {
    int32_t *previous = &definition->first_active;
    while (*previous != -1 && *previous < tag)
        previous = &definition->events[*previous].next_active;
    if (*previous == tag) return;
    definition->events[tag].next_active = *previous;
    *previous = tag;
}

static void pattern_active_unlink(pattern_definition_t *definition,
                                  int32_t tag) {
    int32_t *previous = &definition->first_active;
    while (*previous != -1 && *previous != tag)
        previous = &definition->events[*previous].next_active;
    if (*previous == tag)
        *previous = definition->events[tag].next_active;
    definition->events[tag].next_active = -1;
}

static bool pattern_index_valid(uint32_t pattern) {
    if (pattern_slots != NULL && pattern < (uint32_t)max_pattern_slots)
        return true;
    fprintf(stderr, "pattern %" PRIu32 " is outside configured range 0..%" PRIi32 "\n",
            pattern, max_pattern_slots > 0 ? max_pattern_slots - 1 : -1);
    return false;
}

static bool pattern_payload_is_leaf(const char *wire) {
    if (wire == NULL || wire[0] == '\0') return false;
    // H is meaningful only at the root ingest boundary; zQ is the pattern
    // control family. zQM is the one leaf exception: it
    // only gates already-running instances and cannot create another level.
    const char *pattern_control = strstr(wire, "zQ");
    bool is_mute = pattern_control == wire && strncmp(wire, "zQM", 3) == 0
        && strstr(wire + 3, "zQ") == NULL;
    if (wire[0] == 'H' || (pattern_control != NULL && !is_mute)) {
        fprintf(stderr, "nested pattern events cannot schedule or trigger patterns\n");
        return false;
    }
    const char *end = strchr(wire, 'Z');
    if (end == NULL || end[1] != '\0') {
        fprintf(stderr, "nested pattern event must be one Z-terminated wire message\n");
        return false;
    }
    return true;
}

uint8_t amy_pattern_begin(uint32_t pattern, uint32_t length_ticks) {
    if (!pattern_index_valid(pattern) || length_ticks == 0) return 0;
    pattern_definition_t *definition = pattern_definition_new(length_ticks);
    if (definition == NULL) return 0;

    amy_grab_lock();
    pattern_definition_t *old_staging = pattern_slots[pattern].staging;
    pattern_slots[pattern].staging = definition;
    amy_release_lock();
    pattern_definition_free(old_staging);
    return 1;
}

uint8_t amy_pattern_add_wire(uint32_t pattern, uint32_t tick,
                             uint32_t period, uint32_t tag, bool has_tag,
                             const char *wire) {
    if (!pattern_index_valid(pattern) || !pattern_payload_is_leaf(wire))
        return 0;

    size_t length = strlen(wire);
    char *copy = (char *)malloc_caps((uint32_t)length + 1,
                                    amy_global.config.ram_caps_events);
    if (copy == NULL) {
        amy_oom("pattern event wire");
        return 0;
    }
    memcpy(copy, wire, length + 1);

    amy_grab_lock();
    pattern_definition_t *definition = pattern_slots[pattern].staging;
    if (definition == NULL) {
        amy_release_lock();
        free(copy);
        fprintf(stderr, "pattern %" PRIu32 " has no staging definition\n",
                pattern);
        return 0;
    }

    if (has_tag) {
        if (tag >= (uint32_t)max_pattern_event_tags) {
            amy_release_lock();
            free(copy);
            fprintf(stderr, "pattern tag %" PRIu32 " is outside configured range 0..%" PRIi32 "\n",
                    tag, max_pattern_event_tags - 1);
            return 0;
        }
    } else {
        // Keep the root sequencer's exact anonymous zero/zero semantics.
        if (tick == 0 && period == 0) {
            amy_release_lock();
            free(copy);
            return 0;
        }
        tag = (uint32_t)(max_pattern_event_tags + definition->anon_cursor);
        definition->anon_cursor =
            (definition->anon_cursor + 1) % AMY_ANON_SEQUENCE_SLOTS;
    }

    sequence_info_t *event = &definition->events[tag];
    if (event->wire != NULL) free(event->wire);
    event->wire = NULL;
    event->tick = 0;
    event->period = 0;
    pattern_active_unlink(definition, (int32_t)tag);
    if (tick == 0 && period == 0) {
        amy_release_lock();
        free(copy);
        return 0;
    }
    event->tick = tick;
    event->period = period;
    event->wire = copy;
    pattern_active_link(definition, (int32_t)tag);
    amy_release_lock();
    return 1;
}

uint8_t amy_pattern_add_event(uint32_t pattern, const amy_event *event) {
    if (event == NULL) return 0;
    amy_event payload = *event;
    uint32_t tick = AMY_IS_SET(payload.ticks[TICKS_TICK])
        ? payload.ticks[TICKS_TICK] : 0;
    uint32_t period = AMY_IS_SET(payload.ticks[TICKS_PERIOD])
        ? payload.ticks[TICKS_PERIOD] : 0;
    bool has_tag = AMY_IS_SET(payload.ticks[TICKS_TAG]);
    uint32_t tag = has_tag ? payload.ticks[TICKS_TAG] : 0;
    AMY_UNSET(payload.ticks[TICKS_TICK]);
    AMY_UNSET(payload.ticks[TICKS_PERIOD]);
    AMY_UNSET(payload.ticks[TICKS_TAG]);

    char *wire = (char *)malloc_caps(MAX_MESSAGE_LEN,
                                     amy_global.config.ram_caps_events);
    if (wire == NULL) {
        amy_oom("pattern add event");
        return 0;
    }
    sprint_event(&payload, wire, MAX_MESSAGE_LEN, /* wirecode= */ true);
    uint8_t result = amy_pattern_add_wire(
        pattern, tick, period, tag, has_tag, wire);
    free(wire);
    return result;
}

uint8_t amy_pattern_commit(uint32_t pattern) {
    if (!pattern_index_valid(pattern)) return 0;
    amy_grab_lock();
    pattern_definition_t *replacement = pattern_slots[pattern].staging;
    if (replacement == NULL) {
        amy_release_lock();
        fprintf(stderr, "pattern %" PRIu32 " has no staging definition\n",
                pattern);
        return 0;
    }
    pattern_definition_t *old = pattern_slots[pattern].current;
    pattern_slots[pattern].current = replacement;
    pattern_slots[pattern].staging = NULL;
    if (old != NULL) old->retired = true;
    bool free_old = old != NULL && old->refs == 0;
    amy_release_lock();
    if (free_old) pattern_definition_free(old);
    return 1;
}

uint8_t amy_pattern_clear(uint32_t pattern) {
    if (!pattern_index_valid(pattern)) return 0;
    amy_grab_lock();
    pattern_definition_t *current = pattern_slots[pattern].current;
    pattern_definition_t *staging = pattern_slots[pattern].staging;
    pattern_slots[pattern].current = NULL;
    pattern_slots[pattern].staging = NULL;
    if (current != NULL) current->retired = true;
    bool free_current = current != NULL && current->refs == 0;
    amy_release_lock();
    pattern_definition_free(staging);
    if (free_current) pattern_definition_free(current);
    return current != NULL || staging != NULL;
}

static bool tick_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

static uint32_t pattern_activation_tick(uint32_t quantum) {
    uint32_t now = amy_global.sequencer_tick_count;
    if (quantum == 0) return wire_firing ? now : now + 1;
    uint32_t remainder = now % quantum;
    if (wire_firing && remainder == 0) return now;
    return now + (remainder == 0 ? quantum : quantum - remainder);
}

uint8_t amy_pattern_trigger(uint32_t pattern, uint8_t mode,
                            uint32_t quantize_ticks,
                            uint32_t instance_tag) {
    if (!pattern_index_valid(pattern)
        || (mode != AMY_PATTERN_ONE_SHOT && mode != AMY_PATTERN_LOOP))
        return 0;

    amy_grab_lock();
    pattern_definition_t *definition = pattern_slots[pattern].current;
    if (definition == NULL) {
        amy_release_lock();
        fprintf(stderr, "pattern %" PRIu32 " is not committed\n", pattern);
        return 0;
    }
    int32_t free_index = -1;
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        if (!pattern_instances[i].occupied) {
            free_index = i;
            break;
        }
    }
    if (free_index < 0) {
        amy_release_lock();
        fprintf(stderr, "no free nested pattern instance\n");
        return 0;
    }

    uint32_t start_tick = pattern_activation_tick(quantize_ticks);
    if (instance_tag != AMY_PATTERN_UNTAGGED) {
        for (int32_t i = 0; i < max_pattern_players; ++i) {
            pattern_instance_t *existing = &pattern_instances[i];
            if (existing->occupied && existing->instance_tag == instance_tag
                && (existing->stop_tick == UINT32_MAX
                    || tick_reached(existing->stop_tick, start_tick))) {
                existing->stop_tick = start_tick;
            }
        }
    }

    pattern_instance_t *instance = &pattern_instances[free_index];
    instance->definition = definition;
    instance->start_tick = start_tick;
    instance->stop_tick = UINT32_MAX;
    instance->instance_tag = instance_tag;
    instance->mode = mode;
    instance->occupied = true;
    definition->refs++;
    amy_release_lock();
    return 1;
}

uint8_t amy_pattern_stop(uint32_t instance_tag, uint32_t quantize_ticks) {
    if (instance_tag == AMY_PATTERN_UNTAGGED) return 0;
    uint32_t stop_tick = pattern_activation_tick(quantize_ticks);
    uint8_t found = 0;
    amy_grab_lock();
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        pattern_instance_t *instance = &pattern_instances[i];
        if (instance->occupied && instance->instance_tag == instance_tag) {
            instance->stop_tick = stop_tick;
            found = 1;
        }
    }
    amy_release_lock();
    return found;
}

uint8_t amy_pattern_mute(uint32_t instance_tag, uint32_t duration_ticks) {
    if (instance_tag == AMY_PATTERN_UNTAGGED) return 0;
    uint32_t now = amy_global.sequencer_tick_count;
    uint8_t found = 0;
    amy_grab_lock();
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        pattern_instance_t *instance = &pattern_instances[i];
        if (!instance->occupied || instance->instance_tag != instance_tag)
            continue;
        instance->mute_tick = now;
        instance->mute_duration = duration_ticks;
        instance->muted = duration_ticks != 0;
        found = 1;
    }
    amy_release_lock();
    return found;
}

uint8_t amy_pattern_schedule(uint32_t pattern, uint8_t mode,
                             uint32_t offset_ticks, uint32_t period_ticks,
                             uint32_t quantize_ticks, uint32_t sequence_tag,
                             uint32_t instance_tag) {
    if (!pattern_index_valid(pattern)
        || (mode != AMY_PATTERN_ONE_SHOT && mode != AMY_PATTERN_LOOP))
        return 0;

    char encoded[96];
    int length;
    if (instance_tag == AMY_PATTERN_UNTAGGED) {
        length = snprintf(encoded, sizeof(encoded), "zQT%" PRIu32 ",%u,0Z",
                          pattern, mode);
    } else {
        length = snprintf(encoded, sizeof(encoded),
                          "zQT%" PRIu32 ",%u,0,%" PRIu32 "Z",
                          pattern, mode, instance_tag);
    }
    if (length < 0 || (size_t)length >= sizeof(encoded)) return 0;
    char *wire = strdup(encoded);
    if (wire == NULL) {
        amy_oom("scheduled pattern trigger");
        return 0;
    }

    uint32_t tick = pattern_activation_tick(quantize_ticks) + offset_ticks;
    if (period_ticks != 0) tick %= period_ticks;
    return sequencer_add_wire(
        tick, period_ticks, sequence_tag, true, wire);
}

static void pattern_instance_release(pattern_instance_t *instance) {
    pattern_definition_t *definition = instance->definition;
    bzero(instance, sizeof(pattern_instance_t));
    if (definition != NULL && definition->refs > 0) definition->refs--;
    if (definition != NULL && definition->retired && definition->refs == 0)
        pattern_definition_free(definition);
}

static void pattern_instances_reset(void) {
    if (pattern_instances == NULL) return;
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        if (pattern_instances[i].occupied)
            pattern_instance_release(&pattern_instances[i]);
    }
}

void sequencer_rebase_patterns(uint32_t old_tick) {
    if (pattern_instances == NULL) return;
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        pattern_instance_t *instance = &pattern_instances[i];
        if (!instance->occupied) continue;
        instance->start_tick -= old_tick;
        if (instance->stop_tick != UINT32_MAX) {
            instance->stop_tick = tick_reached(old_tick, instance->stop_tick)
                ? 0 : instance->stop_tick - old_tick;
        }
        if (instance->muted) instance->mute_tick -= old_tick;
    }
}

static bool pattern_instance_running(const pattern_instance_t *instance,
                                     uint32_t tick) {
    if (!instance->occupied || !tick_reached(tick, instance->start_tick))
        return false;
    if (instance->stop_tick != UINT32_MAX
        && tick_reached(tick, instance->stop_tick))
        return false;
    uint32_t elapsed = tick - instance->start_tick;
    return instance->mode == AMY_PATTERN_LOOP
        || elapsed < instance->definition->length_ticks;
}

static bool pattern_instance_audible(const pattern_instance_t *instance,
                                     uint32_t tick) {
    if (!pattern_instance_running(instance, tick)) return false;
    if (instance->muted
        && tick - instance->mute_tick < instance->mute_duration)
        return false;
    return true;
}

static bool pattern_event_hits(const pattern_instance_t *instance,
                               const sequence_info_t *event, uint32_t tick) {
    uint32_t local_tick = tick - instance->start_tick;
    if (instance->mode == AMY_PATTERN_LOOP)
        local_tick %= instance->definition->length_ticks;
    return event->period != 0
        ? local_tick % event->period == event->tick
        : local_tick == event->tick;
}

static bool pattern_event_is_mute(const sequence_info_t *event) {
    return event->wire != NULL && strncmp(event->wire, "zQM", 3) == 0;
}

static void pattern_process_tick(uint32_t tick) {
    if (pattern_instances == NULL) return;

    // Retire stopped/finished instances before processing events. A
    // replacement scheduled on this exact tick takes effect here.
    amy_grab_lock();
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        pattern_instance_t *instance = &pattern_instances[i];
        if (!instance->occupied || !tick_reached(tick, instance->start_tick))
            continue;
        uint32_t elapsed = tick - instance->start_tick;
        bool stopped = instance->stop_tick != UINT32_MAX
            && tick_reached(tick, instance->stop_tick);
        bool finished = instance->mode == AMY_PATTERN_ONE_SHOT
            && elapsed >= instance->definition->length_ticks;
        if (stopped || finished) pattern_instance_release(instance);
    }
    amy_release_lock();

    // Mute is a schedulable leaf control.  Fire every due mute before any
    // ordinary child event so the target cannot leak an onset on the first
    // muted tick merely because its instance occupies an earlier pool slot.
    for (int32_t i = 0; i < max_pattern_players; ++i) {
        amy_grab_lock();
        pattern_instance_t *instance = &pattern_instances[i];
        if (!pattern_instance_audible(instance, tick)) {
            amy_release_lock();
            continue;
        }
        pattern_definition_t *definition = instance->definition;
        definition->refs++;
        amy_release_lock();

        int32_t tag = definition->first_active;
        while (tag != -1) {
            sequence_info_t *event = &definition->events[tag];
            int32_t next = event->next_active;
            if (pattern_event_is_mute(event)
                && pattern_event_hits(instance, event, tick))
                amy_play_message(event->wire);
            tag = next;
        }

        amy_grab_lock();
        if (definition->refs > 0) definition->refs--;
        bool free_definition = definition->retired && definition->refs == 0;
        amy_release_lock();
        if (free_definition) pattern_definition_free(definition);
    }

    for (int32_t i = 0; i < max_pattern_players; ++i) {
        amy_grab_lock();
        pattern_instance_t *instance = &pattern_instances[i];
        if (!pattern_instance_audible(instance, tick)) {
            amy_release_lock();
            continue;
        }
        pattern_definition_t *definition = instance->definition;
        uint32_t local_tick = tick - instance->start_tick;
        if (instance->mode == AMY_PATTERN_LOOP)
            local_tick %= definition->length_ticks;
        // Keep the immutable definition alive while ordinary event playback
        // runs without the lock. A payload can itself execute
        // RESET_SEQUENCER, which releases the instance that owned this ref.
        definition->refs++;
        amy_release_lock();

        int32_t tag = definition->first_active;
        while (tag != -1) {
            sequence_info_t *event = &definition->events[tag];
            int32_t next = event->next_active;
            bool hit = event->period != 0
                ? local_tick % event->period == event->tick
                : local_tick == event->tick;
            if (hit && event->wire != NULL && !pattern_event_is_mute(event))
                amy_play_message(event->wire);
            tag = next;
        }

        amy_grab_lock();
        if (definition->refs > 0) definition->refs--;
        bool free_definition = definition->retired && definition->refs == 0;
        amy_release_lock();
        if (free_definition) pattern_definition_free(definition);
    }
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
    // Root items fire first.  A root event can therefore start a pattern on
    // this exact tick; the new instance then emits its local tick-zero events
    // below.  Pattern payloads cannot trigger another pattern, fixing nesting
    // at two levels and keeping the processing cost bounded.
    pattern_process_tick(amy_global.sequencer_tick_count);
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
        amy_grab_lock();
        sequencer_rebase_patterns(amy_global.sequencer_tick_count);
        amy_global.sequencer_tick_count = 0;
        amy_release_lock();
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
