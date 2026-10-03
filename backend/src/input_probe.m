// Process-local counters, plus an optional fast path for mouse look. Every event
// still reaches the game's own Unity handler. No event tap, cursor warping, or
// device-setting changes.
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void pg3d_presentation_log(const char *message);
extern void pg3d_latency_motion(double event_timestamp);
static void (*original_send_event)(id, SEL, NSEvent *);
static uint64_t motion_count, button_count, fast_count;
static uint64_t lock_counts[4];
static double motion_total, motion_max, motion_age_total, motion_age_max, fast_total;
static BOOL installed;
// All mouse motion handed to the game; the frame profiler compares it per frame.
volatile uint64_t pg3d_motion_events;

// Fast mouse look. For motion events while Unity has the cursor locked, call the
// game's own handler (-[PlayerAppDelegate DidSendEvent:], which the game view's
// mouseMoved:/mouseDragged: call first) directly instead of routing through
// NSApplication and NSWindow. That routing only updates window tracking areas
// and resize cursors, which cannot change while the pointer is locked.
static BOOL fast_requested, fast_ready;
static char fast_reason[160] = "not checked";
static const char *mouse_control;
static Class player_window_class, player_view_class, player_delegate_class;
static int (*cursor_lock_state)(void);
static const char *unity_image = "/UnityPlayer.dylib"; // Unit tests point this at the test binary.

static BOOL is_motion(NSEventType type) {
    return type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDragged ||
           type == NSEventTypeRightMouseDragged || type == NSEventTypeOtherMouseDragged;
}

static BOOL fast_path_applies(NSEvent *event, int lock) {
    if (!fast_requested || !fast_ready) return NO;
    if (lock != 1) return NO; // CursorLockMode.Locked only: the pointer cannot move.
    NSWindow *window = event.window;
    if (window == nil || ![window isKindOfClass:player_window_class] || !window.isKeyWindow ||
        window.attachedSheet != nil || NSApp.modalWindow != nil || !NSApp.isActive) return NO;
    if (![window.firstResponder isKindOfClass:player_view_class]) return NO;
    return [NSApp.delegate isKindOfClass:player_delegate_class];
}

static void measured_send_event(id application, SEL selector, NSEvent *event) {
    NSEventType type = event.type;
    if (!is_motion(type)) {
        if (type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ||
            type == NSEventTypeOtherMouseDown) ++button_count;
        original_send_event(application, selector, event);
        return;
    }
    double start = CACurrentMediaTime();
    double stamp = event.timestamp;
    double age = NSProcessInfo.processInfo.systemUptime - stamp;
    pg3d_latency_motion(stamp);
    int lock = cursor_lock_state ? cursor_lock_state() : -1;
    ++lock_counts[lock >= 0 && lock <= 2 ? lock : 3];
    BOOL fast = fast_path_applies(event, lock);
    if (fast) {
        // Exactly what the game view does first on mouseMoved:/mouseDragged:. When it
        // returns NO the view only forwards to NSResponder, which ignores motion.
        ((BOOL (*)(id, SEL, NSEvent *))objc_msgSend)(NSApp.delegate, @selector(DidSendEvent:), event);
    } else {
        original_send_event(application, selector, event);
    }
    double duration = CACurrentMediaTime() - start;
    ++pg3d_motion_events;
    ++motion_count;
    motion_total += duration;
    if (fast) { ++fast_count; fast_total += duration; }
    if (duration > motion_max) motion_max = duration;
    if (age >= 0 && age < 60) {
        motion_age_total += age;
        if (age > motion_age_max) motion_age_max = age;
    }
}

// Every step of the normal motion route must still be Unity's or AppKit's own
// code, so that skipping the AppKit part cannot skip another library's hook.
static BOOL route_step_is(Class cls, SEL selector, const char *image) {
    IMP implementation = class_getMethodImplementation(cls, selector);
    Dl_info info = {0};
    return implementation && dladdr((const void *)implementation, &info) && info.dli_fname &&
           strstr(info.dli_fname, image) != NULL;
}

// macOS may give NSApp or its delegate a runtime subclass (for example for key-value
// observing), so accept Unity's classes anywhere in the class chain.
static BOOL is_or_inherits(Class cls, const char *name) {
    for (; cls; cls = class_getSuperclass(cls)) if (strcmp(class_getName(cls), name) == 0) return YES;
    return NO;
}

static const char *validate_fast_path(Class application_class) {
    if (!is_or_inherits(application_class, "PlayerApplication")) return "application is not Unity's PlayerApplication";
    id delegate = NSApp.delegate;
    Class delegate_class = delegate ? object_getClass(delegate) : Nil;
    player_delegate_class = objc_getClass("PlayerAppDelegate");
    if (!delegate_class || !player_delegate_class || !is_or_inherits(delegate_class, "PlayerAppDelegate"))
        return "application delegate is not Unity's PlayerAppDelegate";
    player_window_class = objc_getClass("PlayerWindow");
    player_view_class = objc_getClass("PlayerWindowView");
    if (!player_window_class || !player_view_class) return "Unity window classes not found";
    Method handler = class_getInstanceMethod(delegate_class, @selector(DidSendEvent:));
    if (!handler || strcmp(method_getTypeEncoding(handler), "c24@0:8@16") != 0) return "DidSendEvent: signature changed";
    const char *unity = unity_image;
    if (!route_step_is(application_class, @selector(sendEvent:), unity) ||
        !route_step_is(delegate_class, @selector(DidSendEvent:), unity) ||
        !route_step_is(player_window_class, @selector(sendEvent:), unity) ||
        !route_step_is(player_view_class, @selector(mouseMoved:), unity) ||
        !route_step_is(player_view_class, @selector(mouseDragged:), unity) ||
        !route_step_is(player_view_class, @selector(rightMouseDragged:), unity) ||
        !route_step_is(player_view_class, @selector(otherMouseDragged:), unity))
        return "a Unity event handler is missing or replaced by another library";
    if (!route_step_is([NSApplication class], @selector(sendEvent:), "/AppKit") ||
        !route_step_is([NSWindow class], @selector(sendEvent:), "/AppKit"))
        return "AppKit event routing is replaced by another library";
    if (!cursor_lock_state) return "Cursor.lockState binding unavailable";
    return NULL;
}

static BOOL read_mouse_control(void) {
    if (!mouse_control) return fast_requested;
    FILE *file = fopen(mouse_control, "r");
    if (!file) return fast_requested;
    char value[16] = {0}, extra;
    BOOL requested = fast_requested;
    if (fscanf(file, "%15s %c", value, &extra) == 1) {
        if (strcmp(value, "fast") == 0) requested = YES;
        else if (strcmp(value, "game") == 0) requested = NO;
    }
    fclose(file);
    return requested;
}

static void apply_mouse_control(void) {
    BOOL requested = read_mouse_control();
    if (requested == fast_requested) return;
    fast_requested = requested;
    char line[240];
    snprintf(line, sizeof(line), "fast mouse look %s%s%s.", requested ? "requested" : "off; all motion routed through AppKit",
             requested && !fast_ready ? " but UNAVAILABLE: " : "", requested && !fast_ready ? fast_reason : "");
    pg3d_presentation_log(line);
}

void pg3d_input_start(void *(*resolve)(const char *), bool observe) {
    if (installed || ![NSThread isMainThread] || NSApp == nil) return;
    Class application_class = object_getClass(NSApp);
    Method method = class_getInstanceMethod(application_class, @selector(sendEvent:));
    if (!method) return;
    if (!observe) {
        cursor_lock_state = resolve ? (int (*)(void))resolve("UnityEngine.Cursor::get_lockState") : NULL;
        const char *problem = validate_fast_path(application_class);
        fast_ready = problem == NULL;
        snprintf(fast_reason, sizeof(fast_reason), "%s", problem ? problem : "validated");
        const char *control = getenv("PG3D_OPT_CONTROL");
        static char path[1100];
        if (control && strlen(control) < 1024) {
            snprintf(path, sizeof(path), "%s.mouse", control);
            mouse_control = path;
        }
    } else {
        snprintf(fast_reason, sizeof(fast_reason), "observation mode");
    }
    original_send_event = (void *)method_getImplementation(method);
    class_replaceMethod(application_class, @selector(sendEvent:), (IMP)measured_send_event,
                        method_getTypeEncoding(method));
    installed = YES;
    pg3d_presentation_log("input probe active: counting process-local AppKit motion events; all events reach the game's handler; no mouse settings changed.");
    char line[320];
    id delegate = NSApp.delegate;
    snprintf(line, sizeof(line), "fast mouse look check: %s (application class %s, delegate class %s).",
             fast_ready ? "validated Unity route" : fast_reason, class_getName(application_class),
             delegate ? class_getName(object_getClass(delegate)) : "none");
    pg3d_presentation_log(line);
    if (!observe) apply_mouse_control();
}

void pg3d_input_sample(double elapsed) {
    if (!installed || elapsed <= 0) return;
    char line[640];
    snprintf(line, sizeof(line), "input sample: motion_events=%llu; motion_hz=%.1f; sendEvent_mean_us=%.2f; sendEvent_max_ms=%.3f; motion_dispatch_ms_per_second=%.2f; event_age_mean_ms=%.3f; event_age_max_ms=%.3f; buttons=%llu; appkit_coalescing=%s; fast_events=%llu; fast_mean_us=%.2f; routed_mean_us=%.2f; cursor_lock none/locked/confined=%llu/%llu/%llu. Counts exclude direct HID/raw input.",
             (unsigned long long)motion_count, motion_count / elapsed,
             motion_count ? motion_total * 1e6 / motion_count : 0, motion_max * 1000,
             motion_total * 1000 / elapsed, motion_count ? motion_age_total * 1000 / motion_count : 0,
             motion_age_max * 1000, (unsigned long long)button_count,
             NSEvent.isMouseCoalescingEnabled ? "on" : "off",
             (unsigned long long)fast_count, fast_count ? fast_total * 1e6 / fast_count : 0,
             motion_count > fast_count ? (motion_total - fast_total) * 1e6 / (motion_count - fast_count) : 0,
             (unsigned long long)lock_counts[0], (unsigned long long)lock_counts[1], (unsigned long long)lock_counts[2]);
    pg3d_presentation_log(line);
    motion_count = button_count = fast_count = 0;
    motion_total = motion_max = motion_age_total = motion_age_max = fast_total = 0;
    memset(lock_counts, 0, sizeof(lock_counts));
    apply_mouse_control();
    snprintf(line, sizeof(line), "fast mouse look status: requested=%s; available=%s; reason=%s.",
             fast_requested ? "yes" : "no", fast_ready ? "yes" : "no", fast_reason);
    pg3d_presentation_log(line);
}
