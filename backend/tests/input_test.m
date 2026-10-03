// Fast mouse look against stand-in Unity classes. Runs without the game.
#include <assert.h>
#include "../src/input_probe.m"
void pg3d_presentation_log(const char *message) { fprintf(stderr, "  log: %s\n", message); }
static unsigned latency_motions;
void pg3d_latency_motion(double event_timestamp) { (void)event_timestamp; ++latency_motions; }
static unsigned app_sends, handler_calls, view_moves;
static int lock_mode = 1;
static int fake_lock(void) { return lock_mode; }
static void *fake_resolve(const char *name) {
    return strcmp(name, "UnityEngine.Cursor::get_lockState") == 0 ? (void *)fake_lock : NULL;
}
@interface PlayerApplication : NSApplication @end
@implementation PlayerApplication
- (void)sendEvent:(NSEvent *)event { ++app_sends; [super sendEvent:event]; }
- (BOOL)isActive { return YES; }
@end
@interface PlayerAppDelegate : NSObject <NSApplicationDelegate> @end
@implementation PlayerAppDelegate
- (BOOL)DidSendEvent:(NSEvent *)event { (void)event; ++handler_calls; return NO; }
@end
@interface PlayerWindow : NSWindow @end
@implementation PlayerWindow
- (void)sendEvent:(NSEvent *)event { [super sendEvent:event]; }
- (BOOL)isKeyWindow { return YES; }
@end
@interface PlayerWindowView : NSView @end
@implementation PlayerWindowView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseMoved:(NSEvent *)event { ++view_moves; [(PlayerAppDelegate *)NSApp.delegate DidSendEvent:event]; }
- (void)mouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
@end
static void write_control(const char *path, const char *value) {
    FILE *file = fopen(path, "w"); assert(file); fputs(value, file); fclose(file);
}
int main(void) {
    @autoreleasepool {
        char control[] = "/private/tmp/optimizer-input-test.XXXXXX";
        assert(mkstemp(control) >= 0);
        char mouse[1100]; snprintf(mouse, sizeof(mouse), "%s.mouse", control);
        write_control(mouse, "game\n");
        setenv("PG3D_OPT_CONTROL", control, 1);
        [PlayerApplication sharedApplication];
        PlayerAppDelegate *delegate = [PlayerAppDelegate new];
        NSApp.delegate = delegate;
        PlayerWindow *window = [[PlayerWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 200)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        PlayerWindowView *view = [[PlayerWindowView alloc] initWithFrame:NSMakeRect(0, 0, 200, 200)];
        window.contentView = view; window.acceptsMouseMovedEvents = YES;
        [window makeFirstResponder:view];
        // macOS can give NSApp and its delegate runtime subclasses (key-value observing).
        Class observed_app = objc_allocateClassPair([PlayerApplication class], "NSKVONotifying_PlayerApplication", 0);
        objc_registerClassPair(observed_app); object_setClass(NSApp, observed_app);
        Class observed_delegate = objc_allocateClassPair([PlayerAppDelegate class], "NSKVONotifying_PlayerAppDelegate", 0);
        objc_registerClassPair(observed_delegate); object_setClass(delegate, observed_delegate);
        unity_image = getprogname();
        pg3d_input_start(fake_resolve, false);
        assert(installed && fast_ready && !fast_requested);
        NSEvent *move = [NSEvent mouseEventWithType:NSEventTypeMouseMoved location:NSMakePoint(50, 50)
            modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber
            context:nil eventNumber:0 clickCount:0 pressure:0];
        assert(move.window == window);

        [NSApp sendEvent:move]; // Game mode: the normal AppKit route reaches the view.
        assert(app_sends == 1 && view_moves == 1 && handler_calls == 1 && pg3d_motion_events == 1);

        write_control(mouse, "fast\n"); pg3d_input_sample(5);
        assert(fast_requested);
        [NSApp sendEvent:move]; // Fast: the game's handler runs once, AppKit routing is skipped.
        assert(app_sends == 1 && view_moves == 1 && handler_calls == 2 && fast_count == 1 && pg3d_motion_events == 2);
        assert(latency_motions == 2); // Both routes report the move to the latency meter.

        lock_mode = 0; [NSApp sendEvent:move]; // Menus (cursor free): normal route again.
        assert(app_sends == 2 && view_moves == 2 && handler_calls == 3 && fast_count == 1);
        lock_mode = 1;

        NSEvent *click = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:NSMakePoint(50, 50)
            modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber
            context:nil eventNumber:1 clickCount:1 pressure:1];
        [NSApp sendEvent:click]; // Buttons are never taken off the normal route.
        assert(app_sends == 3 && button_count == 1);

        write_control(mouse, "game\n"); pg3d_input_sample(5);
        assert(!fast_requested);
        [NSApp sendEvent:move];
        assert(app_sends == 4 && view_moves == 3 && handler_calls == 4);

        // A handler outside Unity's image makes the fast path unavailable.
        installed = NO; unity_image = "/UnityPlayer.dylib";
        pg3d_input_start(fake_resolve, false);
        assert(!fast_ready);
        unlink(mouse); unlink(control);
    }
    puts("PASS: fast mouse look calls the game's handler once and skips AppKit routing only while the cursor is locked; buttons, free cursor and game mode use the normal route; foreign handlers disable it; runtime subclasses of the Unity classes are accepted.");
}
