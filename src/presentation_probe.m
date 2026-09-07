// Observe actual Metal presentation without changing scheduling or layer state.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

extern void pg3d_presentation_log(const char *text);

enum { kMaxLayers = 8, kRecentTimes = 256 };
typedef struct {
    uintptr_t identity;
    uint64_t acquired, shown, duplicate, dropped, unsupported;
    double recent[kRecentTimes];
    unsigned next_time;
    BOOL display_sync, transaction;
    NSUInteger drawable_count;
    CGSize size;
} LayerSample;

static LayerSample g_layers[kMaxLayers];
static unsigned g_layer_count;
static uint64_t g_untracked;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static id<CAMetalDrawable> (*g_next_drawable)(id, SEL);
static BOOL g_enabled;

static void record_presented(unsigned slot, double timestamp) {
    pthread_mutex_lock(&g_lock);
    LayerSample *sample = &g_layers[slot];
    if (!(timestamp > 0.0) || !isfinite(timestamp)) {
        ++sample->dropped;
    } else {
        BOOL duplicate = NO;
        for (unsigned i = 0; i < kRecentTimes; ++i) {
            if (sample->recent[i] == timestamp) {
                duplicate = YES;
                break;
            }
        }
        if (duplicate) {
            ++sample->duplicate;
        } else {
            ++sample->shown;
            sample->recent[sample->next_time++ % kRecentTimes] = timestamp;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static id<CAMetalDrawable> observed_next_drawable(CAMetalLayer *layer, SEL cmd) {
    id<CAMetalDrawable> drawable = g_next_drawable(layer, cmd);
    if (drawable == nil) return nil;

    uintptr_t identity = (uintptr_t)(__bridge void *)layer;
    BOOL display_sync = layer.displaySyncEnabled;
    BOOL transaction = layer.presentsWithTransaction;
    NSUInteger drawable_count = 0;
    if (@available(macOS 10.13.2, *)) drawable_count = layer.maximumDrawableCount;
    CGSize size = layer.drawableSize;
    pthread_mutex_lock(&g_lock);
    unsigned slot = 0;
    while (slot < g_layer_count && g_layers[slot].identity != identity) ++slot;
    if (slot == kMaxLayers) {
        ++g_untracked;
        pthread_mutex_unlock(&g_lock);
        return drawable;
    }
    if (slot == g_layer_count) {
        g_layers[slot].identity = identity;
        ++g_layer_count;
    }
    LayerSample *sample = &g_layers[slot];
    ++sample->acquired;
    sample->display_sync = display_sync;
    sample->transaction = transaction;
    sample->drawable_count = drawable_count;
    sample->size = size;
    pthread_mutex_unlock(&g_lock);

    if (@available(macOS 10.15.4, *)) {
        if ([drawable respondsToSelector:@selector(addPresentedHandler:)]) {
            // Capture only the slot, never the drawable that owns this block.
            [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
                record_presented(slot, presented.presentedTime);
            }];
            return drawable;
        }
    }
    pthread_mutex_lock(&g_lock);
    ++g_layers[slot].unsupported;
    pthread_mutex_unlock(&g_lock);
    return drawable;
}

void pg3d_presentation_start(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        if (@available(macOS 10.15.4, *)) {
            Method method = class_getInstanceMethod([CAMetalLayer class],
                                                    @selector(nextDrawable));
            if (method == NULL) {
                pg3d_presentation_log("presentation probe unavailable: no CAMetalLayer nextDrawable");
                return;
            }
            g_next_drawable = (void *)method_getImplementation(method);
            method_setImplementation(method, (IMP)observed_next_drawable);
            g_enabled = YES;
            pg3d_presentation_log("presentation probe active: observing Metal drawable presentation timestamps");
        } else {
            pg3d_presentation_log("presentation probe requires macOS 10.15.4 or later");
        }
    });
}

static void report_screen(void) {
    NSWindow *window = NSApp.mainWindow ?: NSApp.keyWindow;
    if (window == nil) {
        CGFloat largest = 0;
        for (NSWindow *candidate in NSApp.windows) {
            CGFloat area = NSWidth(candidate.frame) * NSHeight(candidate.frame);
            if (candidate.isVisible && area > largest) {
                window = candidate;
                largest = area;
            }
        }
    }
    NSScreen *screen = window.screen;
    if (screen == nil) {
        pg3d_presentation_log("presentation screen unavailable: game has no onscreen window yet");
        return;
    }
    CGDirectDisplayID display = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    double mode_hz = mode != NULL ? CGDisplayModeGetRefreshRate(mode) : 0.0;
    if (mode != NULL) CGDisplayModeRelease(mode);
    char line[512];
    if (@available(macOS 12.0, *)) {
        snprintf(line, sizeof(line),
                 "presentation screen: display=%u max_fps=%ld interval_ms=%.3f..%.3f mode_hz=%.3f fullscreen=%d (mode_hz=0 can indicate variable refresh)",
                 display, (long)screen.maximumFramesPerSecond,
                 screen.minimumRefreshInterval * 1000.0,
                 screen.maximumRefreshInterval * 1000.0, mode_hz,
                 (window.styleMask & NSWindowStyleMaskFullScreen) != 0);
    } else {
        snprintf(line, sizeof(line), "presentation screen: display=%u mode_hz=%.3f fullscreen=%d",
                 display, mode_hz, (window.styleMask & NSWindowStyleMaskFullScreen) != 0);
    }
    pg3d_presentation_log(line);
}

void pg3d_presentation_sample(double elapsed) {
    if (!g_enabled || !(elapsed > 0.0)) return;
    LayerSample snapshot[kMaxLayers];
    pthread_mutex_lock(&g_lock);
    unsigned count = g_layer_count;
    uint64_t untracked = g_untracked;
    g_untracked = 0;
    for (unsigned i = 0; i < count; ++i) {
        snapshot[i] = g_layers[i];
        g_layers[i].acquired = g_layers[i].shown = g_layers[i].duplicate = 0;
        g_layers[i].dropped = g_layers[i].unsupported = 0;
    }
    pthread_mutex_unlock(&g_lock);

    char line[640];
    if (count == 0) pg3d_presentation_log("presentation sample: no Metal drawables observed yet");
    for (unsigned i = 0; i < count; ++i) {
        LayerSample *s = &snapshot[i];
        snprintf(line, sizeof(line),
                 "presentation sample: layer=%u/%u id=0x%llx presented_fps=%.1f acquired_fps=%.1f duplicate=%llu dropped=%llu unsupported=%llu displaySyncEnabled=%d presentsWithTransaction=%d maximumDrawableCount=%lu size=%.0fx%.0f",
                 i + 1, count, (unsigned long long)s->identity, s->shown / elapsed,
                 s->acquired / elapsed, (unsigned long long)s->duplicate,
                 (unsigned long long)s->dropped, (unsigned long long)s->unsupported,
                 s->display_sync, s->transaction, (unsigned long)s->drawable_count,
                 s->size.width, s->size.height);
        pg3d_presentation_log(line);
    }
    if (untracked != 0) {
        snprintf(line, sizeof(line), "presentation sample: %llu drawables exceeded the %u-layer tracking limit",
                 (unsigned long long)untracked, kMaxLayers);
        pg3d_presentation_log(line);
    }
    if ([NSThread isMainThread]) report_screen();
    else dispatch_async(dispatch_get_main_queue(), ^{ report_screen(); });
}
