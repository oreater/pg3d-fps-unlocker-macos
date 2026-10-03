// Individual post-processing effects against a fake IL2CPP object model.
#include <assert.h>
#include <stddef.h>
#include "../src/options.c"
#include "../src/volumes.c"
#define G PG3D_GAME_VALUE
unsigned pg3d_ssao_found, pg3d_ssao_enabled;
void pg3d_presentation_log(const char *s) { (void)s; }
enum { F_INSTANCE=1, F_VOLUMES, F_SHARED, F_INTERNAL, F_SETTINGS, F_ACTIVE, F_CACHED, F_ITEMS, F_SIZE };
typedef struct { void *header[4]; void *items[8]; } FakeArray;
typedef struct { const char *name; FakeArray *array; int32_t size; } FakeList;
typedef struct Obj { const char *name; bool alive, active; struct Obj *shared, *internal; FakeList *list; } Obj;
static FakeArray effect_array, volume_array;
static FakeList effect_list = {"List", &effect_array, 0}, volume_list = {"List", &volume_array, 1};
static Obj ao={"AmbientOcclusion",true,true,NULL,NULL,NULL}, bloom={"Bloom",true,false,NULL,NULL,NULL}, grading={"ColorGrading",true,true,NULL,NULL,NULL},
           vignette={"Vignette",true,true,NULL,NULL,NULL}, ssr={"ScreenSpaceReflections",true,true,NULL,NULL,NULL}, custom={"CustomEffect",true,true,NULL,NULL,NULL};
static Obj profile={"Profile",true,false,NULL,NULL,&effect_list}, volume={"Volume",true,false,&profile,&profile,NULL},
           manager={"Manager",true,false,NULL,NULL,&volume_list};
static void *roots[256];
static unsigned next_root=1;
static void *fake_find(void *image, const char *space, const char *name) { (void)image; (void)space; return (void *)name; }
static void *fake_domain(void) { return NULL; }
static const void *assembly;
static const void **fake_assemblies(void *domain, size_t *count) { (void)domain; *count=1; return &assembly; }
static void *fake_image(const void *a) { (void)a; return NULL; }
static void *fake_field(void *klass, const char *name) {
    (void)klass;
    static const char *names[] = {"", "s_Instance", "m_Volumes", "sharedProfile", "m_InternalProfile", "settings", "active", "m_CachedPtr", "_items", "_size"};
    for (uintptr_t i=1; i<sizeof(names)/sizeof(names[0]); ++i) if (strcmp(name, names[i])==0) return (void *)i;
    return NULL;
}
static void fake_get(void *object, void *field, void *out) {
    Obj *o = object; FakeList *l = object;
    switch ((uintptr_t)field) {
    case F_CACHED: *(void **)out = o->alive ? object : NULL; break;
    case F_VOLUMES: *(void **)out = o->list; break;
    case F_SHARED: *(void **)out = o->shared; break;
    case F_INTERNAL: *(void **)out = o->internal; break;
    case F_SETTINGS: *(void **)out = o->list; break;
    case F_ACTIVE: *(bool *)out = o->active; break;
    case F_ITEMS: *(void **)out = l->array; break;
    case F_SIZE: *(int32_t *)out = l->size; break;
    default: assert(0);
    }
}
static void fake_set(void *object, void *field, void *value) {
    assert((uintptr_t)field == F_ACTIVE && ((Obj *)object)->alive);
    ((Obj *)object)->active = *(bool *)value;
}
static void fake_static(void *field, void *out) { assert((uintptr_t)field == F_INSTANCE); *(void **)out = &manager; }
static void *fake_class(void *object) { return object; }
static const char *fake_name(void *klass) { return ((Obj *)klass)->name; }
static uint32_t fake_header(void) { return offsetof(FakeArray, items); }
static uint32_t fake_length(void *array) { (void)array; return 8; }
static uint32_t root(void *obj, bool pinned) { (void)pinned; assert(next_root<256); roots[next_root]=obj; return next_root++; }
static void *target(uint32_t n) { return roots[n]; }
static void unroot(uint32_t n) { roots[n]=NULL; }
static void apply(void) { pg3d_volumes_apply(); }
int main(void) {
    domain_get=fake_domain; domain_assemblies=fake_assemblies; assembly_image=fake_image; class_from_name=fake_find;
    class_field=fake_field; field_value=fake_get; set_field_value=fake_set; static_field_value=fake_static;
    object_class=fake_class; class_name=fake_name; array_header_size=fake_header; array_length=fake_length;
    handle_new=root; handle_target=target; handle_free=unroot;
    Obj *all[] = {&ao, &bloom, &grading, &vignette, &ssr, &custom};
    for (unsigned i=0; i<6; ++i) effect_array.items[i]=all[i];
    effect_list.size=6; volume_array.items[0]=&volume;
    pg3d_volumes_start(); assert(ready);

    apply(); // All "game": nothing changes, counts reported.
    assert(ao.active && pg3d_option_status[OPT_AMBIENT_OCCLUSION].state==OPT_STATE_GAME);
    assert(pg3d_option_status[OPT_AMBIENT_OCCLUSION].game==1); // Shared and instance profile are the same object: counted once.
    assert(!pg3d_volumes_managing());

    pg3d_options[OPT_AMBIENT_OCCLUSION]=0; pg3d_options[OPT_BLOOM]=0; apply();
    assert(!ao.active && !bloom.active && grading.active && custom.active);
    assert(pg3d_option_status[OPT_AMBIENT_OCCLUSION].state==OPT_STATE_APPLIED);
    assert(pg3d_option_status[OPT_BLOOM].state==OPT_STATE_ALREADY); // The game had it off already.
    assert(pg3d_volumes_managing());
    ao.active=true; pg3d_volumes_frame(); assert(!ao.active); // Game re-activation handled before rendering.
    pg3d_options[OPT_AMBIENT_OCCLUSION]=G; apply(); assert(ao.active && !bloom.active); // Restored to the game's own flags.

    pg3d_ssao_found=2; pg3d_ssao_enabled=0; pg3d_options[OPT_AMBIENT_OCCLUSION]=0; apply();
    assert(pg3d_option_status[OPT_AMBIENT_OCCLUSION].game==3 && pg3d_option_status[OPT_AMBIENT_OCCLUSION].current==0);
    pg3d_ssao_found=pg3d_ssao_enabled=0; pg3d_options[OPT_AMBIENT_OCCLUSION]=G; apply();

    pg3d_options[OPT_LENS_EFFECTS]=0; pg3d_options[OPT_REFLECTIONS]=0;
    pg3d_option_status[OPT_REFLECTIONS].state=OPT_STATE_ALREADY; // Probes already off (set by the quality settings).
    apply();
    assert(!vignette.active && !ssr.active && pg3d_option_status[OPT_LENS_EFFECTS].state==OPT_STATE_APPLIED);
    assert(pg3d_option_status[OPT_REFLECTIONS].state==OPT_STATE_APPLIED); // Screen-space reflections turned off.
    vignette.alive=false; pg3d_volumes_frame(); apply(); // Destroyed effect: forgotten, never written.
    assert(!vignette.active);
    pg3d_options[OPT_LENS_EFFECTS]=G; pg3d_options[OPT_REFLECTIONS]=G; pg3d_options[OPT_BLOOM]=G; apply();
    assert(ssr.active && !bloom.active && !pg3d_volumes_managing());

    manager.list=NULL; apply(); // No volumes yet (loading): nothing found, no crash.
    assert(pg3d_option_status[OPT_COLOR_GRADING].game==0);
    puts("PASS: effect flags off/restore, already-off effects, per-frame re-activation, SSAO counts, lens and SSR, destroyed effects, missing volume list.");
}
