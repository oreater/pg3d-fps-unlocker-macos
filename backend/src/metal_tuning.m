// GPU scheduling for the game's own Metal command queues.
//
// Metal command queues on Apple GPUs carry a scheduling priority (private
// -[IOGPUMetalCommandQueue setGPUPriority:]; 0 high, 1 normal, 2 low). Measured
// on this Mac with another process keeping the GPU busy, a queue at 0 got its
// work scheduled sooner than one at the default 1; in the game's lobby under
// that load, FPS rose 27% and the 95th-percentile frame time fell from 13 to
// 5 ms. Only 0 or the queue's own original value is ever written, and only when
// the original reads back as 1 or 2; Metal asserts on values it does not accept.
//
// (1.7 development also tried a 2-drawable CAMetalLayer queue for lower latency;
// in the lobby it cut FPS by 28% and added latency, so it was removed.)
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include "options.h"

extern void pg3d_presentation_log(const char *message);

// ---- GPU priority --------------------------------------------------------
enum { QUEUE_LIMIT = 8, HIGH_GPU_PRIORITY = 0 };
typedef struct {
    __unsafe_unretained id queue;
    unsigned generation;   // option generation this queue was last brought in line with
    unsigned long game;    // the queue's own priority
    unsigned long current; // readback after the last change (never read later: the queue may be gone)
    bool owned, rejected;
} Queue;
// Queues are compared by address only, and touched only inside the game's own
// call on them, so a queue the game releases is never used afterwards.
static Queue queues[QUEUE_LIMIT];
static _Atomic unsigned queue_count;
static _Atomic unsigned priority_generation = 1;
static _Atomic bool want_high_priority;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static bool priority_supported;
static char priority_note[120] = "not checked";
static id (*original_command_buffer)(id, SEL);
static id (*original_unretained_buffer)(id, SEL);
static id (*original_descriptor_buffer)(id, SEL, id);

static unsigned long read_priority(id queue) {
    return ((unsigned long (*)(id, SEL))objc_msgSend)(queue, @selector(getGPUPriority));
}
static BOOL write_priority(id queue, unsigned long value) {
    return ((BOOL (*)(id, SEL, unsigned long))objc_msgSend)(queue, @selector(setGPUPriority:), value);
}

static void log_queue(const char *action, unsigned long before, unsigned long after, unsigned long game) {
    char line[200];
    snprintf(line, sizeof(line), "gpu priority %s: command queue %lu -> %lu (readback; 0 high, 1 normal, 2 low); game value %lu.",
             action, before, after, game);
    pg3d_presentation_log(line);
}

// Slow path, under the lock: bring one queue in line with the option.
static void settle_queue(id queue, unsigned generation) {
    pthread_mutex_lock(&queue_lock);
    unsigned count = atomic_load(&queue_count);
    Queue *q = NULL;
    for (unsigned i = 0; i < count; ++i) if (queues[i].queue == queue) q = &queues[i];
    if (!q) {
        if (count == QUEUE_LIMIT) { pthread_mutex_unlock(&queue_lock); return; }
        q = &queues[count];
        *q = (Queue){.queue = queue, .game = read_priority(queue)};
        q->rejected = q->game > 2; // an unknown starting value: leave it alone
        atomic_store(&queue_count, count + 1);
    }
    unsigned long current = read_priority(queue);
    if (q->owned && current != HIGH_GPU_PRIORITY) q->owned = false, q->game = current; // changed by the game
    bool high = atomic_load(&want_high_priority);
    if (!q->rejected && high && !q->owned && q->game != HIGH_GPU_PRIORITY) {
        BOOL accepted = write_priority(queue, HIGH_GPU_PRIORITY);
        unsigned long readback = read_priority(queue);
        if (!accepted || readback != HIGH_GPU_PRIORITY) {
            if (readback != current) write_priority(queue, current);
            q->rejected = true;
            log_queue("REJECTED", current, readback, q->game);
        } else {
            q->owned = true;
            log_queue("raised", current, readback, q->game);
        }
    } else if (!high && q->owned) {
        write_priority(queue, q->game);
        q->owned = false;
        log_queue("restored", current, read_priority(queue), q->game);
    }
    q->current = read_priority(queue);
    q->generation = generation;
    pthread_mutex_unlock(&queue_lock);
}

// Every command buffer the game creates passes its queue here first.
static inline void note_queue(id queue) {
    unsigned generation = atomic_load_explicit(&priority_generation, memory_order_acquire);
    unsigned count = atomic_load_explicit(&queue_count, memory_order_acquire);
    for (unsigned i = 0; i < count; ++i)
        if (queues[i].queue == queue && queues[i].generation == generation) return;
    settle_queue(queue, generation);
}
static id hooked_command_buffer(id queue, SEL cmd) {
    note_queue(queue);
    return original_command_buffer(queue, cmd);
}
static id hooked_unretained_buffer(id queue, SEL cmd) {
    note_queue(queue);
    return original_unretained_buffer(queue, cmd);
}
static id hooked_descriptor_buffer(id queue, SEL cmd, id descriptor) {
    note_queue(queue);
    return original_descriptor_buffer(queue, cmd, descriptor);
}

static void hook(Class cls, SEL selector, IMP replacement, void *original) {
    Method method = class_getInstanceMethod(cls, selector);
    if (method) *(IMP *)original = method_setImplementation(method, replacement);
}

// Main thread, once the game's Metal device exists. A throwaway queue from the
// same device shows the concrete queue class the game's queues use.
void pg3d_metal_tuning_start(void) {
    static bool started;
    if (started) return;
    started = true;
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> probe = [device newCommandQueue];
        Class cls = probe ? object_getClass(probe) : Nil;
        if (!cls) snprintf(priority_note, sizeof(priority_note), "no Metal command queue");
        else if (![probe respondsToSelector:@selector(setGPUPriority:)] || ![probe respondsToSelector:@selector(getGPUPriority)])
            snprintf(priority_note, sizeof(priority_note), "%s has no GPU priority", class_getName(cls));
        else {
            unsigned long normal = read_priority(probe);
            if (normal != 1) snprintf(priority_note, sizeof(priority_note), "unexpected default priority %lu", normal);
            else {
                hook(cls, @selector(commandBuffer), (IMP)hooked_command_buffer, &original_command_buffer);
                hook(cls, @selector(commandBufferWithUnretainedReferences), (IMP)hooked_unretained_buffer, &original_unretained_buffer);
                hook(cls, @selector(commandBufferWithDescriptor:), (IMP)hooked_descriptor_buffer, &original_descriptor_buffer);
                priority_supported = original_command_buffer || original_unretained_buffer || original_descriptor_buffer;
                snprintf(priority_note, sizeof(priority_note), priority_supported ? "%s" : "%s: command buffer methods not found",
                         class_getName(cls));
            }
        }
        char line[220];
        snprintf(line, sizeof(line), "gpu priority check: %s (%s).", priority_supported ? "available" : "UNAVAILABLE", priority_note);
        pg3d_presentation_log(line);
    }
}

// Main thread, whenever the options change and every five seconds.
void pg3d_metal_tuning_apply(void) {
    bool high = pg3d_options[OPT_GPU_PRIORITY] == 1;
    if (atomic_exchange(&want_high_priority, high) != high) atomic_fetch_add(&priority_generation, 1);
    OptionStatus *gpu = &pg3d_option_status[OPT_GPU_PRIORITY];
    pthread_mutex_lock(&queue_lock);
    unsigned count = atomic_load(&queue_count), owned = 0, rejected = 0;
    for (unsigned i = 0; i < count; ++i) owned += queues[i].owned, rejected += queues[i].rejected;
    // Report the first queue (Unity's main one): its own priority and the current one.
    gpu->game = count ? (double)queues[0].game : -1;
    gpu->current = count ? (double)queues[0].current : -1;
    pthread_mutex_unlock(&queue_lock);
    if (!priority_supported) gpu->state = high ? OPT_STATE_UNAVAILABLE : OPT_STATE_GAME;
    else if (!high || !count) gpu->state = OPT_STATE_GAME;
    else if (rejected == count) gpu->state = OPT_STATE_REJECTED;
    else gpu->state = owned ? OPT_STATE_APPLIED : OPT_STATE_ALREADY;
}
