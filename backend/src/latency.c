// Mouse-to-screen latency. Measures, for every frame the game renders, how long
// after its input the frame actually appeared on screen:
//   render-to-screen: from the moment the main thread starts rendering a frame
//                     (just before UpdateAllRenderers) to the drawable's presentedTime;
//   mouse-to-screen:  from the hardware timestamp of the newest mouse move that frame
//                     used to the same presentedTime (frames with new motion only).
// Event timestamps, mach_absolute_time and Metal's presentedTime share one clock.
//
// Frames are handed from the main thread to Unity's render thread through a
// small single-producer/single-consumer queue: one entry per rendered frame,
// taken when the render thread acquires that frame's drawable. Nothing here
// changes what the game does; it only reads clocks.
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void pg3d_presentation_log(const char *message);

typedef struct { double rendered, motion; } LatencyMark; // seconds on the host clock; motion 0 = none

enum { RING = 8, RENDER_LAG_LIMIT = 2, SAMPLE_LIMIT = 8192 };
static LatencyMark ring[RING];
static _Atomic unsigned ring_head, ring_tail;      // head: next write (main); tail: next read (render)
static _Atomic uint64_t newest_motion_bits;        // double, written on the main thread
static double used_motion;                         // main thread only
static pthread_mutex_t samples_lock = PTHREAD_MUTEX_INITIALIZER;
static float render_samples[SAMPLE_LIMIT], mouse_samples[SAMPLE_LIMIT];
static unsigned render_count, mouse_count;
// Frame pacing: the time between consecutive frames reaching the render stage
// (main thread only). Spikes here are what a player feels as hitches while aiming.
static float frame_samples[SAMPLE_LIMIT];
static unsigned frame_count;
static double previous_frame;
static _Atomic uint64_t unmatched;

double pg3d_host_seconds(void) {
    static mach_timebase_info_data_t base;
    if (base.denom == 0) mach_timebase_info(&base);
    return (double)mach_absolute_time() * base.numer / base.denom / 1e9;
}

// Main thread, for every mouse move handed to the game.
void pg3d_latency_motion(double event_timestamp) {
    if (!(event_timestamp > 0) || !isfinite(event_timestamp)) return;
    uint64_t bits;
    memcpy(&bits, &event_timestamp, sizeof(bits));
    atomic_store_explicit(&newest_motion_bits, bits, memory_order_relaxed);
}

// Main thread, once per frame just before rendering. Events are only handed to
// the game between frames, so every move seen so far is input to this frame.
void pg3d_latency_frame(void) {
    uint64_t bits = atomic_load_explicit(&newest_motion_bits, memory_order_relaxed);
    double motion;
    memcpy(&motion, &bits, sizeof(motion));
    LatencyMark mark = {pg3d_host_seconds(), motion != used_motion ? motion : 0};
    used_motion = motion;
    double interval = mark.rendered - previous_frame;
    if (previous_frame > 0 && interval < 0.25 && frame_count < SAMPLE_LIMIT) frame_samples[frame_count++] = (float)interval;
    previous_frame = mark.rendered;
    unsigned head = atomic_load_explicit(&ring_head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&ring_tail, memory_order_acquire);
    if (head - tail >= RING) return; // Render thread gone quiet (window hidden): skip.
    ring[head % RING] = mark;
    atomic_store_explicit(&ring_head, head + 1, memory_order_release);
}

// Render thread, when it acquires a drawable: the oldest frame not yet drawn.
// Unity's render thread trails the main thread by at most a frame; anything
// older is a frame that never acquired a drawable, so drop it to stay aligned.
bool pg3d_latency_take(LatencyMark *out) {
    unsigned tail = atomic_load_explicit(&ring_tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&ring_head, memory_order_acquire);
    if (head == tail) return false;
    if (head - tail > RENDER_LAG_LIMIT) {
        atomic_fetch_add_explicit(&unmatched, head - tail - RENDER_LAG_LIMIT, memory_order_relaxed);
        tail = head - RENDER_LAG_LIMIT;
    }
    *out = ring[tail % RING];
    atomic_store_explicit(&ring_tail, tail + 1, memory_order_release);
    return true;
}

// Any thread (Metal's presented handler). presentedTime 0 means never shown.
void pg3d_latency_presented(LatencyMark mark, double presented) {
    if (!(presented > mark.rendered) || presented - mark.rendered > 0.5) return;
    pthread_mutex_lock(&samples_lock);
    if (render_count < SAMPLE_LIMIT) render_samples[render_count++] = (float)(presented - mark.rendered);
    if (mark.motion > 0 && presented > mark.motion && presented - mark.motion < 0.5 && mouse_count < SAMPLE_LIMIT)
        mouse_samples[mouse_count++] = (float)(presented - mark.motion);
    pthread_mutex_unlock(&samples_lock);
}

static int by_value(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}
static double percentile(float *values, unsigned count, double p) {
    return count ? values[(unsigned)fmin(count - 1, floor(p * count))] * 1000.0 : 0;
}

// Every five seconds from the unlocker's timer (main thread, like the frames).
void pg3d_latency_sample(void) {
    static float render[SAMPLE_LIMIT], mouse[SAMPLE_LIMIT];
    unsigned frames = frame_count;
    frame_count = 0;
    if (frames) {
        qsort(frame_samples, frames, sizeof(float), by_value);
        char pacing[240];
        snprintf(pacing, sizeof(pacing), "frame pacing: frames=%u; frame_ms median=%.3f p95=%.3f p99=%.3f max=%.3f.",
                 frames, percentile(frame_samples, frames, 0.5), percentile(frame_samples, frames, 0.95),
                 percentile(frame_samples, frames, 0.99), frame_samples[frames - 1] * 1000.0);
        pg3d_presentation_log(pacing);
    }
    pthread_mutex_lock(&samples_lock);
    unsigned renders = render_count, mice = mouse_count;
    memcpy(render, render_samples, renders * sizeof(float));
    memcpy(mouse, mouse_samples, mice * sizeof(float));
    render_count = mouse_count = 0;
    pthread_mutex_unlock(&samples_lock);
    uint64_t dropped = atomic_exchange_explicit(&unmatched, 0, memory_order_relaxed);
    if (renders == 0) return;
    qsort(render, renders, sizeof(float), by_value);
    qsort(mouse, mice, sizeof(float), by_value);
    char line[400];
    snprintf(line, sizeof(line),
             "input latency: frames=%u; render_to_screen_ms median=%.2f p95=%.2f; mouse_frames=%u; mouse_to_screen_ms median=%.2f p95=%.2f; unmatched=%llu. From the move's hardware timestamp (or the start of rendering) to Metal's presentedTime.",
             renders, percentile(render, renders, 0.5), percentile(render, renders, 0.95), mice,
             percentile(mouse, mice, 0.5), percentile(mouse, mice, 0.95), (unsigned long long)dropped);
    pg3d_presentation_log(line);
}
