// Mouse-to-screen latency meter: frame hand-off from the main thread to the
// render thread, alignment when drawables are skipped, and the logged numbers.
#include <assert.h>
#include <unistd.h>
#include "../src/latency.c"
static char last_line[512];
void pg3d_presentation_log(const char *message) { snprintf(last_line, sizeof(last_line), "%s", message); }
static double number_after(const char *marker) {
    const char *at = strstr(last_line, marker);
    assert(at);
    return atof(at + strlen(marker));
}
int main(void) {
    LatencyMark mark;
    assert(!pg3d_latency_take(&mark)); // Nothing rendered yet.

    // A frame without mouse movement: render-to-screen only.
    pg3d_latency_frame();
    assert(pg3d_latency_take(&mark) && mark.motion == 0 && mark.rendered > 0);
    pg3d_latency_presented(mark, mark.rendered + 0.004);

    // A move before the next frame: that frame carries the move's timestamp once.
    double move = pg3d_host_seconds();
    pg3d_latency_motion(move);
    pg3d_latency_frame();
    pg3d_latency_frame(); // No new move: the next frame has none.
    assert(pg3d_latency_take(&mark) && mark.motion == move);
    pg3d_latency_presented(mark, move + 0.006);
    assert(pg3d_latency_take(&mark) && mark.motion == 0);
    pg3d_latency_presented(mark, 0); // Never shown: not counted.

    // The render thread may trail by one frame; older frames never drew and are dropped.
    for (int i = 0; i < 5; ++i) pg3d_latency_frame();
    assert(pg3d_latency_take(&mark));
    assert(atomic_load(&unmatched) == 3);
    assert(pg3d_latency_take(&mark) && !pg3d_latency_take(&mark));

    // Nonsense timestamps are ignored.
    pg3d_latency_motion(-1);
    pg3d_latency_motion(NAN);
    pg3d_latency_presented((LatencyMark){10, 0}, 9);
    pg3d_latency_presented((LatencyMark){10, 0}, 11);

    static char lines[2][512];
    // Capture both lines: frame pacing first, then latency.
    pg3d_latency_sample();
    snprintf(lines[1], sizeof(lines[1]), "%s", last_line);
    assert(strstr(lines[1], "input latency: frames=2;"));
    assert(fabs(number_after("render_to_screen_ms median=") - 6) < 2.5);
    assert(strstr(last_line, "mouse_frames=1;"));
    assert(fabs(number_after("mouse_to_screen_ms median=") - 6) < 0.01);
    assert(strstr(last_line, "unmatched=3."));
    (void)lines;
    last_line[0] = 0;
    pg3d_latency_sample();
    assert(last_line[0] == 0); // Nothing rendered or presented in this window: no line.
    for (int i = 0; i < 4; ++i) { pg3d_latency_frame(); usleep(2000); }
    pg3d_latency_sample(); // Frames but no drawables: pacing only (the gap since the frames above counts too).
    assert(strstr(last_line, "frame pacing: frames=4;") && number_after("median=") >= 2.0);

    // A hidden window: the main thread keeps rendering and no drawable is taken.
    for (int i = 0; i < 100; ++i) pg3d_latency_frame();
    assert(atomic_load(&ring_head) - atomic_load(&ring_tail) == RING);
    assert(pg3d_latency_take(&mark));
    puts("PASS: latency meter hand-off, motion frames, skipped drawables, hidden window, filtering, report, frame pacing.");
}
