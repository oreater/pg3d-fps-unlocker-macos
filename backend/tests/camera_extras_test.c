// Camera extras (depth texture / HDR) and the cached frame-guard path.
#include <assert.h>
#include <unistd.h>
#include "../src/options.c"
#include "../src/effects.c"
#define G PG3D_GAME_VALUE
static void tick(int postfx, int depth) { pg3d_options[OPT_POSTFX]=postfx; pg3d_options[OPT_DEPTH_PASS]=depth; pg3d_effects_tick(); }
void pg3d_presentation_log(const char *s) { (void)s; }
typedef struct { bool native_alive, enabled, hdr, target; int depth; } Fake;
static Fake first={true,true,true,false,1}, second={true,true,true,false,1};
static void *roots[256];
static unsigned next_root=1;
static struct { void *header[4]; void *items[2]; } camera_array;
static bool reversed;
static void *camera(void) { return &first; }
static void *game_object(void *cam) { return cam; }
static void *component(void *cam, void *type) { (void)type; return cam; }
static bool read_enabled(void *o) { assert(((Fake *)o)->native_alive); return ((Fake *)o)->enabled; }
static void write_enabled(void *o, bool v) { assert(((Fake *)o)->native_alive); ((Fake *)o)->enabled=v; }
static int read_depth(void *o) { assert(((Fake *)o)->native_alive); return ((Fake *)o)->depth; }
static void write_depth(void *o, int v) { assert(((Fake *)o)->native_alive); ((Fake *)o)->depth=v; }
static bool read_hdr(void *o) { assert(((Fake *)o)->native_alive); return ((Fake *)o)->hdr; }
static void write_hdr(void *o, bool v) { assert(((Fake *)o)->native_alive); ((Fake *)o)->hdr=v; }
static void *read_target(void *o) { return ((Fake *)o)->target ? o : NULL; }
static void read_field(void *obj, void *field, void *out) { (void)field; *(void **)out=((Fake *)obj)->native_alive ? obj : NULL; }
static uint32_t root(void *obj, bool pinned) { (void)pinned; assert(next_root<256); roots[next_root]=obj; return next_root++; }
static void *target(uint32_t n) { return roots[n]; }
static void unroot(uint32_t n) { roots[n]=NULL; }
static int active_count(void) { return 2; }
static uint32_t header_size(void) { return offsetof(__typeof__(camera_array),items); }
static void *make_array(void *klass, uintptr_t count) { (void)klass; assert(count==CAMERA_LIMIT); return &camera_array; }
static int fill_array(void *array) {
    assert(array==&camera_array);
    camera_array.items[0]=reversed ? &second : &first;
    camera_array.items[1]=reversed ? &first : &second;
    return 2;
}
int main(void) {
    main_camera=camera;get_game_object=game_object;get_component=component;
    get_enabled=read_enabled;set_enabled=write_enabled;field_value=read_field;
    handle_new=root;handle_target=target;handle_free=unroot;
    camera_count=active_count;fill_cameras=fill_array;camera_class=(void *)1;
    array_new=make_array;array_header_size=header_size;
    get_depth_mode=read_depth;set_depth_mode=write_depth;get_hdr=read_hdr;set_hdr=write_hdr;
    get_target_texture=read_target;
    effects[0].type_handle=root((void *)1,false);ready=true;

    tick(G, G); // Balanced never touches camera extras.
    assert(first.enabled && first.depth==1 && first.hdr);
    tick(0, G);
    assert(!first.enabled && !second.enabled);
    assert(first.depth==0 && !first.hdr && second.depth==0 && !second.hdr);

    fast_frames=rescan_frames=reasserted=0;
    pg3d_effects_frame(); assert(fast_frames==1 && rescan_frames==0 && reasserted==0);
    first.enabled=true; first.depth=3; second.hdr=true; // Game resets during respawn.
    pg3d_effects_frame();
    assert(fast_frames==2 && reasserted==3 && !first.enabled && first.depth==0 && !second.hdr);
    reversed=true; pg3d_effects_frame(); assert(rescan_frames==1); // Camera list changed: full scan.
    pg3d_effects_frame(); assert(fast_frames==3);

    tick(G, G); // Leaving Performance restores observed values.
    assert(first.enabled && first.depth==3 && first.hdr && second.depth==1 && second.hdr);
    fast_frames=0; pg3d_effects_frame(); assert(fast_frames==0 && rescan_frames==0); // Inactive outside Performance.

    // Depth pre-pass alone: depth off on every screen camera, HDR untouched.
    tick(G, 0);
    assert(first.enabled && first.depth==0 && first.hdr && second.depth==0 && second.hdr);
    assert(pg3d_option_status[OPT_DEPTH_PASS].state==OPT_STATE_APPLIED);
    fast_frames=0; first.depth=1; pg3d_effects_frame(); assert(fast_frames==1 && first.depth==0);
    tick(0, 0); // Post-processing off as well: HDR off on cameras whose stack is off.
    assert(!first.enabled && first.depth==0 && !first.hdr);
    tick(G, 0); // Stack back on: HDR restored, depth stays off.
    assert(first.enabled && first.hdr && first.depth==0 && second.hdr);
    tick(G, G); assert(first.depth==3 && second.depth==1 && first.hdr); // Depth restored.
    assert(pg3d_option_status[OPT_DEPTH_PASS].state==OPT_STATE_GAME);
    tick(G, G);
    second.target=true; second.depth=1; second.hdr=true;
    tick(0, G); assert(second.depth==1 && second.hdr); // Render-texture cameras are left alone.
    second.native_alive=false; tick(0, G); // Destroyed cameras are released, never called.
    tick(G, G); assert(first.enabled && first.depth==3 && first.hdr);
    puts("PASS: camera extras apply/restore, game resets, depth pre-pass option, render-texture cameras, cached frame path and rescans.");
}
