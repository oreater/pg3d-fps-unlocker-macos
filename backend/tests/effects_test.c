#include <assert.h>
#include <unistd.h>
#include "../src/effects.c"
void pg3d_presentation_log(const char *s) { (void)s; }
typedef struct { bool native_alive, enabled, occlusion; } Fake;
static Fake first={true,true,true}, second={true,true,true};
static Fake *current=&first;
static void *roots[128];
static unsigned next_root=1, restores;
static void *camera(void) { return current; }
static void *game_object(void *cam) { return cam; }
static void *component(void *cam, void *type) { (void)type; return cam; }
static bool read_enabled(void *obj) { assert(((Fake *)obj)->native_alive); return ((Fake *)obj)->enabled; }
static void write_enabled(void *obj, bool v) { assert(((Fake *)obj)->native_alive); ((Fake *)obj)->enabled=v; if(v)++restores; }
static bool read_occlusion(void *obj) { assert(((Fake *)obj)->native_alive); return ((Fake *)obj)->occlusion; }
static void write_occlusion(void *obj, bool value) { assert(((Fake *)obj)->native_alive); ((Fake *)obj)->occlusion=value; }
static void read_field(void *obj, void *field, void *out) { (void)field; *(void **)out=((Fake *)obj)->native_alive ? obj : NULL; }
static uint32_t root(void *obj, bool pinned) { (void)pinned; assert(next_root<128);roots[next_root]=obj;return next_root++; }
static void *target(uint32_t n) { return roots[n]; }
static void unroot(uint32_t n) { roots[n]=NULL; }
static struct { void *header[4]; void *items[2]; } camera_array;
static bool reversed;
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
    main_camera=camera;get_game_object=game_object;get_component=component;get_enabled=read_enabled;set_enabled=write_enabled;
    field_value=read_field;handle_new=root;handle_target=target;handle_free=unroot;
    effects[0].type_handle=root((void *)1,false);ready=true;
    pg3d_effects_tick(1);assert(first.enabled); // Balanced preserves post-processing.
    pg3d_effects_tick(2);assert(!first.enabled);
    pg3d_effects_tick(0);assert(first.enabled && restores==1);
    first.enabled=false;pg3d_effects_tick(2);pg3d_effects_tick(0);assert(!first.enabled); // Originally disabled stays disabled.
    first.enabled=true;pg3d_effects_tick(2);current=&second;pg3d_effects_tick(2);
    assert(!first.enabled && !second.enabled); // Pooled/inactive camera keeps its override through death.
    second.native_alive=false;current=NULL;pg3d_effects_tick(2); // Never call native methods on destroyed components.
    assert(effects[0].component_handle!=0); // Live inactive first camera remains tracked.
    current=&first;first.enabled=true;pg3d_effects_frame();assert(!first.enabled); // Respawn reset handled before rendering.
    pg3d_effects_tick(0);assert(first.enabled); // Explicit Original restores inactive and active cameras.
    first.enabled=true;current=&first;pg3d_effects_tick(2);first.enabled=true;pg3d_effects_tick(2);pg3d_effects_tick(1);
    assert(first.enabled); // Reapply and restore after an observed game reset.
    char directory[]="/tmp/optimizer-effects-test.XXXXXX";
    assert(mkdtemp(directory));
    char control[512], file[600];snprintf(control,sizeof(control),"%s/control",directory);
    snprintf(file,sizeof(file),"%s.occlusion",control);setenv("PG3D_OPT_CONTROL",control,1);
    get_occlusion=read_occlusion;set_occlusion=write_occlusion;
    FILE *output=fopen(file,"w");assert(output);fputs("off\n",output);fclose(output);
    pg3d_effects_tick(1);assert(!first.occlusion);
    output=fopen(file,"w");assert(output);fputs("game\n",output);fclose(output);
    pg3d_effects_tick(1);assert(first.occlusion && !culling_camera_handle);
    unlink(file);rmdir(directory);unsetenv("PG3D_OPT_CONTROL");
    second.native_alive=true; second.enabled=true;
    camera_count=active_count;fill_cameras=fill_array;camera_class=(void *)1;
    array_new=make_array;array_header_size=header_size;
    pg3d_effects_tick(2);assert(!first.enabled && !second.enabled);
    reversed=true;pg3d_effects_tick(2);assert(!first.enabled && !second.enabled);
    pg3d_effects_tick(0);assert(first.enabled && second.enabled);
    puts("PASS: postfx restoration, pre-disabled state, camera changes, destroyed components, game reset, culling restoration, multiple cameras and reordering.");
}
