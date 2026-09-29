#include <assert.h>
#include "../src/frame_guard.c"
volatile uint64_t pg3d_motion_events;
static unsigned updates, renders;
static char last_stage_line[1400];
void pg3d_presentation_log(const char *message) {
    if (strstr(message, "stages (ms/frame")) snprintf(last_stage_line, sizeof(last_stage_line), "%s", message);
}
void pg3d_optimizer_frame(void) { ++updates; }
static void render_stage(void) { assert(updates==renders+1); ++renders; }

// A small player loop in Unity's flattened pre-order layout: root, then each
// phase followed by its leaves. FixedUpdate has a loop condition.
typedef struct { const char *name; void *parent; } FakeClass;
static FakeClass init={"Initialization",0}, fixed={"FixedUpdate",0}, update={"Update",0}, post={"PostLateUpdate",0},
    time_class={"PlayerUpdateTime",&init}, physics_class={"PhysicsFixedUpdate",&fixed},
    script_class={"ScriptRunBehaviourUpdate",&update}, render_class={"UpdateAllRenderers",&post},
    layout_class={"PlayerLoopSystemInternal",0};
static unsigned time_calls, physics_calls, script_calls, set_calls;
static void time_stage(void) { ++time_calls; }
static void physics_stage(void) { ++physics_calls; }
static void script_stage(void) { ++script_calls; }
static void (*script_variable)(void) = script_stage; // Pointer-style update function.
typedef struct { void *type, *delegate, *function, *condition; int32_t children, pad; } Entry;
static struct { char header[32]; Entry e[9]; } loop;
static void build_loop(void) {
    memset(&loop, 0, sizeof(loop));
    loop.e[0] = (Entry){.children=4};
    loop.e[1] = (Entry){.type=&init, .children=1};
    loop.e[2] = (Entry){.type=&time_class, .function=(void *)time_stage};
    loop.e[3] = (Entry){.type=&fixed, .condition=(void *)1, .children=1};
    loop.e[4] = (Entry){.type=&physics_class, .function=(void *)physics_stage};
    loop.e[5] = (Entry){.type=&update, .children=1};
    loop.e[6] = (Entry){.type=&script_class, .function=(void *)&script_variable};
    loop.e[7] = (Entry){.type=&post, .children=1};
    loop.e[8] = (Entry){.type=&render_class, .function=(void *)render_stage};
}
static void call_entry(int i) {
    Entry *e = &loop.e[i];
    if (e->function == (void *)&script_variable || e->function == (void *)&stages[2].slot ||
        e->function == (void *)&guard_slot) (*(void (**)(void))e->function)();
    else ((void (*)(void))e->function)();
}
static void run_frame(void) { call_entry(2); call_entry(4); call_entry(4); call_entry(6); call_entry(8); }
static bool fake_unity(void *p) {
    return p == (void *)time_stage || p == (void *)physics_stage || p == (void *)script_stage || p == (void *)render_stage;
}
static int dummy;
static void *fake_domain(void) { return &dummy; }
static const void *assembly = &dummy;
static const void **fake_assemblies(void *d, size_t *n) { (void)d; *n=1; return &assembly; }
static void *fake_image(const void *a) { (void)a; return &dummy; }
static void *fake_find(void *image, const char *space, const char *name) {
    (void)image; (void)space;
    return strcmp(name, "PlayerLoopSystemInternal") == 0 ? (void *)&layout_class :
           strcmp(name, "PostLateUpdate") == 0 ? (void *)&post : NULL;
}
static void *fake_nested(void *klass, void **iterator) {
    if (klass != &post || *iterator) return NULL;
    *iterator = &dummy; return &render_class;
}
static const char *fake_name(void *klass) { return ((FakeClass *)klass)->name; }
static const void *fake_class_type(void *klass) { return klass; }
static void *fake_type_object(const void *type) { return (void *)type; }
static void *fake_system_type(void *type) { return type; }
static void *fake_declaring(void *klass) { return ((FakeClass *)klass)->parent; }
static const char *field_names[] = {"type","updateDelegate","updateFunction","loopConditionFunction","numSubSystems"};
static void *fake_field(void *klass, const char *name) {
    (void)klass;
    for (int i=0; i<5; i++) if (strcmp(name, field_names[i]) == 0) return (void *)field_names[i];
    return NULL;
}
static size_t fake_offset(void *field) {
    for (size_t i=0; i<5; i++) if (field == (void *)field_names[i]) return 16 + i*8;
    return 0;
}
static int32_t fake_size(void *klass, uint32_t *alignment) { (void)klass; *alignment=8; return 40; }
static uint32_t fake_header(void) { return 16; }
static uint32_t fake_array_header(void) { return 32; }
static uintptr_t fake_length(void *array) { assert(array == &loop); return 9; }
static uint32_t fake_root(void *object, bool pinned) { (void)object; (void)pinned; return 7; }
static void fake_unroot(uint32_t handle) { assert(handle == 7); }
static void *fake_get_loop(void) { return &loop; }
static void fake_set_loop(void *array) { assert(array == &loop); ++set_calls; }
static void *fake_resolve(const char *name) {
    if (strstr(name, "GetCurrentPlayerLoopInternal")) return (void *)fake_get_loop;
    if (strstr(name, "SetPlayerLoopInternal")) return (void *)fake_set_loop;
    return NULL;
}
static void bind_fakes(void) {
    domain_get=fake_domain; assemblies_get=fake_assemblies; image_get=fake_image; class_find=fake_find;
    nested_get=fake_nested; class_name=fake_name; class_type=fake_class_type; type_object=fake_type_object;
    field_find=fake_field; field_offset=fake_offset; value_size=fake_size; object_header=fake_header;
    array_header=fake_array_header; array_length=fake_length; root_new=fake_root; root_free=fake_unroot;
    system_type_class=fake_system_type; declaring_class=fake_declaring; unity_code=fake_unity;
}
static void reset_guard(void) {
    attempted=installed=profiling=false; stage_count=0; frame_start_stage=UINT_MAX;
    memset(stages, 0, sizeof(stages)); memset(&window_totals, 0, sizeof(window_totals));
    memset(&session_totals, 0, sizeof(session_totals));
    frame_start=loop_end=seen_motion=quiet_run=0; frame_kind=TRANSITION;
    updates=renders=time_calls=physics_calls=script_calls=set_calls=0; build_loop();
}

int main(void) {
    original_stage=render_stage;
    before_render(); before_render();
    assert(updates==2 && renders==2 && frame_calls==2);
    pg3d_frame_guard_sample(); assert(frame_calls==0 && frame_ns==0);
    pg3d_frame_guard_start(NULL,true); assert(!attempted && !installed);
    pg3d_frame_guard_start(NULL,false); assert(attempted && !installed);
    assert(updates==2 && renders==2);

    // Guard only: the profiler is off unless requested.
    bind_fakes(); reset_guard(); unsetenv("PG3D_STAGE_PROFILE");
    pg3d_frame_guard_start(fake_resolve,false);
    assert(installed && !profiling && set_calls==1);
    assert(loop.e[8].function==(void *)before_render && loop.e[2].function==(void *)time_stage);
    run_frame(); assert(renders==1 && updates==1 && time_calls==1 && script_calls==1);

    // Profiler: every native leaf is wrapped and still runs exactly once per call.
    reset_guard(); setenv("PG3D_STAGE_PROFILE","1",1);
    pg3d_frame_guard_start(fake_resolve,false);
    assert(installed && profiling && stage_count==4 && frame_start_stage==0 && stages[1].looped);
    assert(strcmp(stages[2].name,"Update.ScriptRunBehaviourUpdate")==0);
    assert(loop.e[2].function==(void *)stage_wrappers[0] && loop.e[4].function==(void *)stage_wrappers[1]);
    assert(loop.e[6].function==(void *)&stages[2].slot && stages[2].slot==stage_wrappers[2]);
    assert(loop.e[8].function==(void *)stage_wrappers[3] && stages[3].original==before_render);
    assert(stages[2].variable==&script_variable && !stages[3].variable);
    for (int f=0; f<40; f++) run_frame();                          // Mouse still.
    for (int f=0; f<40; f++) { ++pg3d_motion_events; run_frame(); } // Motion before every frame.
    assert(time_calls==80 && physics_calls==160 && script_calls==80 && renders==80 && updates==80);
    assert(window_totals.frames[STILL]==38 && window_totals.frames[TRANSITION]==2 && window_totals.frames[MOVING]==40);
    pg3d_frame_guard_sample();
    assert(strstr(last_stage_line,"Update.ScriptRunBehaviourUpdate") && strstr(last_stage_line,"PostLateUpdate.UpdateAllRenderers"));
    assert(window_totals.frames[MOVING]==0 && stages[0].ticks[MOVING]==0 && session_totals.frames[MOVING]==40);
    unsetenv("PG3D_STAGE_PROFILE");
    puts("PASS: correction precedes render, original stage forwarded exactly once, observation and unavailable bindings do not install, stage profiler wraps every native leaf once and splits frames by mouse motion.");
}
