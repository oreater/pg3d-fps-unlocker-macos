// Camera extras (depth texture / HDR) and the cached frame-guard path.
#include <assert.h>
#include <unistd.h>
#include "../src/effects.c"
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
static void write_control(const char *path, const char *value) {
    FILE *f=fopen(path,"w"); assert(f); fputs(value,f); fclose(f);
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

    pg3d_effects_tick(1); // Balanced never touches camera extras.
    assert(first.enabled && first.depth==1 && first.hdr);
    pg3d_effects_tick(2);
    assert(!first.enabled && !second.enabled);
    assert(first.depth==0 && !first.hdr && second.depth==0 && !second.hdr);

    fast_frames=rescan_frames=reasserted=0;
    pg3d_effects_frame(); assert(fast_frames==1 && rescan_frames==0 && reasserted==0);
    first.enabled=true; first.depth=3; second.hdr=true; // Game resets during respawn.
    pg3d_effects_frame();
    assert(fast_frames==2 && reasserted==3 && !first.enabled && first.depth==0 && !second.hdr);
    reversed=true; pg3d_effects_frame(); assert(rescan_frames==1); // Camera list changed: full scan.
    pg3d_effects_frame(); assert(fast_frames==3);

    pg3d_effects_tick(1); // Leaving Performance restores observed values.
    assert(first.enabled && first.depth==3 && first.hdr && second.depth==1 && second.hdr);
    fast_frames=0; pg3d_effects_frame(); assert(fast_frames==0 && rescan_frames==0); // Inactive outside Performance.

    char directory[]="/tmp/optimizer-extras-test.XXXXXX";
    assert(mkdtemp(directory));
    char control[512], file[600];
    snprintf(control,sizeof(control),"%s/control",directory);
    snprintf(file,sizeof(file),"%s.extras",control);
    setenv("PG3D_OPT_CONTROL",control,1);
    write_control(file,"off\n");
    pg3d_effects_tick(2); // Extras switched off: post-processing only.
    assert(!first.enabled && first.depth==3 && first.hdr);
    write_control(file,"on\n");
    pg3d_effects_tick(2); assert(first.depth==0 && !first.hdr);
    write_control(file,"off\n");
    pg3d_effects_tick(2); assert(first.depth==3 && first.hdr); // Turning extras off restores them.
    unlink(file); rmdir(directory); unsetenv("PG3D_OPT_CONTROL");

    pg3d_effects_tick(0);
    second.target=true; second.depth=1; second.hdr=true;
    pg3d_effects_tick(2); assert(second.depth==1 && second.hdr); // Render-texture cameras are left alone.
    second.native_alive=false; pg3d_effects_tick(2); // Destroyed cameras are released, never called.
    pg3d_effects_tick(0); assert(first.enabled && first.depth==3 && first.hdr);
    puts("PASS: camera extras apply/restore, game resets, extras control, render-texture cameras, cached frame path and rescans.");
}
