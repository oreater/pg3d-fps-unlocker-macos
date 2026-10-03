// Engine thread priority. Unity gives its job workers ("Job.Worker N") its
// "normal" priority, which it maps to 25: below macOS's default of 31. When other
// threads compete for the CPU (Unity's 16 background workers, Steam, Discord, a
// browser), those workers start late and get preempted while the main thread
// waits for their jobs, for example the collider sync before every raycast.
// "High" lifts the job workers and Unity's render thread (the main thread waits
// for it at the end of each frame) to 45, Unity's own highest level. Background
// loading workers, audio and everything else keep their priority.
//
// Threads are found by the names Unity gives them; the name is read through
// Mach (safe for any thread), and pthread calls are used only for Unity's own
// long-lived engine threads. Every change is read back; "Game" restores the
// priority Unity chose.
#include <mach/mach.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "options.h"

extern void pg3d_presentation_log(const char *message);

enum { TRACK_LIMIT = 64, HIGH_PRIORITY = 45 };
typedef struct {
    uint64_t id;            // THREAD_IDENTIFIER_INFO thread_id (port names can be reused)
    int game;               // the priority Unity chose
    int applied;            // what we set, or 0
    bool rejected;
    unsigned scan;          // the last scan that found the thread
    char name[32];
} Tracked;
static Tracked tracked[TRACK_LIMIT];
static unsigned tracked_count, scan_number;
static bool rejected_logged;
// Unit tests replace the thread list and the pthread calls.
static kern_return_t (*list_threads)(task_t, thread_act_array_t *, mach_msg_type_number_t *) = task_threads;
static pthread_t (*thread_for_port)(mach_port_t) = pthread_from_mach_thread_np;
static int (*get_param)(pthread_t, int *, struct sched_param *) = pthread_getschedparam;
static int (*set_param)(pthread_t, int, const struct sched_param *) = pthread_setschedparam;

static bool engine_thread(const char *name) {
    return strncmp(name, "Job.Worker ", 11) == 0 || strcmp(name, "UnityGfxDeviceWorker") == 0;
}

static Tracked *track(uint64_t id, const char *name, int priority) {
    for (unsigned i = 0; i < tracked_count; ++i)
        if (tracked[i].id == id) return &tracked[i];
    if (tracked_count == TRACK_LIMIT) return NULL;
    Tracked *t = &tracked[tracked_count++];
    *t = (Tracked){.id = id, .game = priority};
    snprintf(t->name, sizeof(t->name), "%s", name);
    return t;
}

static void log_change(const Tracked *t, const char *action, int before, int after) {
    char line[200];
    snprintf(line, sizeof(line), "engine threads %s: %s priority %d -> %d (readback); Unity's value %d.",
             action, t->name, before, after, t->game);
    pg3d_presentation_log(line);
}

// One thread: returns its current priority after any change, or -1 when skipped.
static int manage(mach_port_t port, bool high, bool *changed) {
    thread_extended_info_data_t extended;
    mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;
    if (thread_info(port, THREAD_EXTENDED_INFO, (thread_info_t)&extended, &count) != KERN_SUCCESS) return -1;
    extended.pth_name[sizeof(extended.pth_name) - 1] = 0;
    if (!engine_thread(extended.pth_name)) return -1;
    thread_identifier_info_data_t identifier;
    count = THREAD_IDENTIFIER_INFO_COUNT;
    if (thread_info(port, THREAD_IDENTIFIER_INFO, (thread_info_t)&identifier, &count) != KERN_SUCCESS) return -1;
    pthread_t thread = thread_for_port(port);
    int policy = 0;
    struct sched_param param = {0};
    if (!thread || get_param(thread, &policy, &param) != 0) return -1;
    Tracked *t = track(identifier.thread_id, extended.pth_name, param.sched_priority);
    if (!t) return -1;
    t->scan = scan_number;
    int current = param.sched_priority;
    // A value other than ours is Unity's own (it can set priorities again later).
    if (!t->applied || current != t->applied) t->game = current;
    if (t->rejected) return current;
    int wanted = high && t->game < HIGH_PRIORITY ? HIGH_PRIORITY : t->game;
    if (current == wanted) {
        t->applied = high && wanted != t->game ? wanted : 0;
        return current;
    }
    struct sched_param next = {.sched_priority = wanted};
    set_param(thread, policy, &next);
    int readback = get_param(thread, &policy, &param) == 0 ? param.sched_priority : -1;
    if (readback != wanted) {
        t->rejected = true;
        t->applied = 0;
        if (readback != current && readback >= 0) {
            next.sched_priority = current;
            set_param(thread, policy, &next);
        }
        if (!rejected_logged) log_change(t, "REJECTED", current, readback);
        rejected_logged = true;
        return current;
    }
    t->applied = wanted != t->game ? wanted : 0;
    *changed = true;
    log_change(t, wanted == t->game ? "restored" : "raised", current, readback);
    return readback;
}

// From the options change handler and every five seconds (new threads, or
// Unity changing a priority back).
void pg3d_threads_apply(void) {
    bool high = pg3d_options[OPT_ENGINE_THREADS] == 1;
    static bool scanned;
    OptionStatus *status = &pg3d_option_status[OPT_ENGINE_THREADS];
    // Off and nothing of ours left to restore: keep the last readback, skip the scan.
    bool any_raised = false;
    for (unsigned i = 0; i < tracked_count; ++i) any_raised |= tracked[i].applied != 0;
    if (!high && !any_raised && scanned) {
        status->state = OPT_STATE_GAME;
        return;
    }
    scanned = true;
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t count = 0;
    if (list_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
        status->state = OPT_STATE_UNAVAILABLE;
        return;
    }
    ++scan_number;
    int lowest_game = 0, lowest_now = 0;
    unsigned found = 0, raised = 0, rejected = 0;
    bool changed = false;
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        int now = manage(threads[i], high, &changed);
        if (now >= 0) {
            ++found;
            if (!lowest_now || now < lowest_now) lowest_now = now;
        }
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(*threads));
    for (unsigned i = 0; i < tracked_count; ++i) {
        if (tracked[i].scan != scan_number) { tracked[i].applied = 0; continue; } // The thread has exited.
        if (!lowest_game || tracked[i].game < lowest_game) lowest_game = tracked[i].game;
        raised += tracked[i].applied != 0;
        rejected += tracked[i].rejected;
    }
    status->game = found ? lowest_game : -1;
    status->current = found ? lowest_now : -1;
    if (!found) status->state = high ? OPT_STATE_UNAVAILABLE : OPT_STATE_GAME;
    else if (!high) status->state = OPT_STATE_GAME;
    else if (rejected == found) status->state = OPT_STATE_REJECTED;
    else status->state = raised ? OPT_STATE_APPLIED : OPT_STATE_ALREADY;
    if (changed) {
        char line[200];
        snprintf(line, sizeof(line), "engine threads: %s; engine threads=%u; raised=%u; rejected=%u; lowest priority now %d.",
                 high ? "high" : "game", found, raised, rejected, lowest_now);
        pg3d_presentation_log(line);
    }
}
