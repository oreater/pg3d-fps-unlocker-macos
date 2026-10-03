// Engine thread priority against real threads named like Unity's.
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include "../src/options.c"
#include "../src/threads.c"
void pg3d_presentation_log(const char *message) { (void)message; }

static _Atomic bool stop;
static void *named(void *name) {
    pthread_setname_np(name);
    struct sched_param param = {.sched_priority = 25}; // Unity's "normal"
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
    while (!atomic_load(&stop)) usleep(1000);
    return NULL;
}
static int priority(pthread_t thread) {
    int policy; struct sched_param param;
    assert(pthread_getschedparam(thread, &policy, &param) == 0);
    return param.sched_priority;
}
static void set_priority(pthread_t thread, int value) {
    struct sched_param param = {.sched_priority = value};
    assert(pthread_setschedparam(thread, SCHED_OTHER, &param) == 0);
}
int main(void) {
    char *names[] = {"Job.Worker 0", "Job.Worker 1", "UnityGfxDeviceWorker", "Background Job.Worker 0", "AudioManager"};
    enum { COUNT = sizeof(names) / sizeof(*names) };
    pthread_t threads[COUNT];
    for (int i = 0; i < COUNT; ++i) assert(pthread_create(&threads[i], NULL, named, names[i]) == 0);
    usleep(50000);
    for (int i = 0; i < COUNT; ++i) assert(priority(threads[i]) == 25);

    pg3d_threads_apply(); // "Game": nothing changes.
    for (int i = 0; i < COUNT; ++i) assert(priority(threads[i]) == 25);
    OptionStatus *status = &pg3d_option_status[OPT_ENGINE_THREADS];
    assert(status->state == OPT_STATE_GAME && status->game == 25 && status->current == 25);

    pg3d_options[OPT_ENGINE_THREADS] = 1;
    pg3d_threads_apply();
    assert(priority(threads[0]) == HIGH_PRIORITY && priority(threads[1]) == HIGH_PRIORITY && priority(threads[2]) == HIGH_PRIORITY);
    assert(priority(threads[3]) == 25 && priority(threads[4]) == 25); // Background loading and audio keep theirs.
    assert(status->state == OPT_STATE_APPLIED && status->game == 25 && status->current == HIGH_PRIORITY);

    set_priority(threads[1], 30); // Unity sets a priority again: that is its new choice.
    pg3d_threads_apply();
    assert(priority(threads[1]) == HIGH_PRIORITY);
    assert(tracked[1].game == 30 || tracked[0].game == 30 || tracked[2].game == 30);

    pg3d_options[OPT_ENGINE_THREADS] = PG3D_GAME_VALUE;
    pg3d_threads_apply(); // "Game" restores what Unity chose.
    assert(priority(threads[0]) == 25 && priority(threads[1]) == 30 && priority(threads[2]) == 25);
    assert(status->state == OPT_STATE_GAME);

    set_priority(threads[0], 47); // Already above "high": left alone.
    pg3d_options[OPT_ENGINE_THREADS] = 1;
    pg3d_threads_apply();
    assert(priority(threads[0]) == 47 && priority(threads[1]) == HIGH_PRIORITY);
    assert(status->state == OPT_STATE_APPLIED);

    atomic_store(&stop, true);
    for (int i = 0; i < COUNT; ++i) pthread_join(threads[i], NULL);
    pg3d_threads_apply(); // The engine threads are gone.
    assert(status->state == OPT_STATE_UNAVAILABLE);
    pg3d_options[OPT_ENGINE_THREADS] = PG3D_GAME_VALUE;
    pg3d_threads_apply(); // Off with nothing raised: no scan, plain game state.
    assert(status->state == OPT_STATE_GAME);
    unsigned scans = scan_number;
    pg3d_threads_apply();
    assert(scan_number == scans); // Exited threads are not rescanned for ever.
    puts("PASS: engine thread priority raise, Unity's own changes, restore, already-high threads, other threads untouched.");
}
