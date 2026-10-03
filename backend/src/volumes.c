// Individual Post Processing Stack v2 effects. Each volume profile lists its
// effects; an effect whose `active` flag is false is skipped when the stack
// blends volumes, so the camera renders without it while the rest of the
// stack stays on. Volumes come from PostProcessManager's own list (no scene
// search). Original flags are restored when an option returns to "game".
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "options.h"

extern void pg3d_presentation_log(const char *message);
extern unsigned pg3d_ssao_found, pg3d_ssao_enabled;

#define PP "UnityEngine.Rendering.PostProcessing"
#define TRACK_LIMIT 256
#define VOLUME_LIMIT 64

typedef struct { const char *name; int option; } EffectKind;
static const EffectKind kinds[] = {
    {"Bloom", OPT_BLOOM},
    {"AmbientOcclusion", OPT_AMBIENT_OCCLUSION},
    {"ColorGrading", OPT_COLOR_GRADING},
    {"AutoExposure", OPT_AUTO_EXPOSURE},
    {"ChromaticAberration", OPT_LENS_EFFECTS},
    {"Vignette", OPT_LENS_EFFECTS},
    {"Grain", OPT_LENS_EFFECTS},
    {"LensDistortion", OPT_LENS_EFFECTS},
    {"DepthOfField", OPT_DOF_MOTION_BLUR},
    {"MotionBlur", OPT_DOF_MOTION_BLUR},
    {"ScreenSpaceReflections", OPT_REFLECTIONS},
};
// Options whose status this module reports (reflections belong to optimizer.c).
static const int owned_options[] = {OPT_BLOOM, OPT_AMBIENT_OCCLUSION, OPT_COLOR_GRADING,
                                    OPT_AUTO_EXPOSURE, OPT_LENS_EFFECTS, OPT_DOF_MOTION_BLUR};

typedef struct {
    uint32_t handle;
    int option;
    bool baseline;
} Tracked;

static Tracked tracked[TRACK_LIMIT];
static void *manager_instance, *manager_volumes, *volume_shared, *volume_internal, *profile_settings,
            *effect_active, *cached_pointer;
static bool ready;

static void *(*domain_get)(void);
static const void **(*domain_assemblies)(void *, size_t *);
static void *(*assembly_image)(const void *);
static void *(*class_from_name)(void *, const char *, const char *);
static void *(*class_field)(void *, const char *);
static void (*field_value)(void *, void *, void *);
static void (*set_field_value)(void *, void *, void *);
static void (*static_field_value)(void *, void *);
static void *(*object_class)(void *);
static const char *(*class_name)(void *);
static uint32_t (*array_header_size)(void);
static uint32_t (*array_length)(void *);
static uint32_t (*handle_new)(void *, bool);
static void *(*handle_target)(uint32_t);
static void (*handle_free)(uint32_t);

void pg3d_volumes_bind(void *handle) {
#define BIND(variable, symbol) *(void **)(&variable) = dlsym(handle, symbol)
    BIND(domain_get, "il2cpp_domain_get");
    BIND(domain_assemblies, "il2cpp_domain_get_assemblies");
    BIND(assembly_image, "il2cpp_assembly_get_image");
    BIND(class_from_name, "il2cpp_class_from_name");
    BIND(class_field, "il2cpp_class_get_field_from_name");
    BIND(field_value, "il2cpp_field_get_value");
    BIND(set_field_value, "il2cpp_field_set_value");
    BIND(static_field_value, "il2cpp_field_static_get_value");
    BIND(object_class, "il2cpp_object_get_class");
    BIND(class_name, "il2cpp_class_get_name");
    BIND(array_header_size, "il2cpp_array_object_header_size");
    BIND(array_length, "il2cpp_array_length");
    BIND(handle_new, "il2cpp_gchandle_new");
    BIND(handle_target, "il2cpp_gchandle_get_target");
    BIND(handle_free, "il2cpp_gchandle_free");
#undef BIND
}

static void *find_class(const char *space, const char *name) {
    size_t count = 0;
    const void **assemblies = domain_assemblies(domain_get(), &count);
    if (!assemblies || count > 4096) return NULL;
    for (size_t i = 0; i < count; ++i) {
        void *klass = class_from_name(assembly_image(assemblies[i]), space, name);
        if (klass) return klass;
    }
    return NULL;
}
static void *field_of(void *klass, const char *name) { return klass ? class_field(klass, name) : NULL; }

static bool alive(void *object) {
    if (!object) return false;
    void *native = NULL;
    field_value(object, cached_pointer, &native);
    return native != NULL;
}

// Copy up to `limit` element pointers out of a managed reference-type array.
static unsigned array_items(void *array, void **out, unsigned limit) {
    if (!array) return 0;
    uint32_t header = array_header_size();
    if (header < 2 * sizeof(void *) || header > 128 || header % sizeof(void *)) return 0;
    uint32_t length = array_length(array);
    unsigned count = length < limit ? length : limit;
    memcpy(out, (char *)array + header, count * sizeof(void *));
    return count;
}
// Copy up to `limit` items out of a managed List<T> of reference types.
static unsigned list_items(void *list, void **out, unsigned limit) {
    if (!list) return 0;
    void *klass = object_class(list);
    void *items = field_of(klass, "_items"), *size = field_of(klass, "_size");
    if (!items || !size) return 0;
    void *array = NULL;
    int32_t count = 0;
    field_value(list, items, &array);
    field_value(list, size, &count);
    if (count <= 0) return 0;
    return array_items(array, out, (unsigned)count < limit ? (unsigned)count : limit);
}

void pg3d_volumes_start(void) {
    if (!domain_get || !domain_assemblies || !assembly_image || !class_from_name || !class_field ||
        !field_value || !set_field_value || !static_field_value || !object_class || !class_name ||
        !array_header_size || !array_length || !handle_new || !handle_target || !handle_free) {
        pg3d_presentation_log("optimizer effects unavailable: missing IL2CPP exports.");
        return;
    }
    void *manager = find_class(PP, "PostProcessManager");
    void *volume = find_class(PP, "PostProcessVolume");
    void *profile = find_class(PP, "PostProcessProfile");
    void *effect = find_class(PP, "PostProcessEffectSettings");
    manager_instance = field_of(manager, "s_Instance");
    manager_volumes = field_of(manager, "m_Volumes");
    volume_shared = field_of(volume, "sharedProfile");
    volume_internal = field_of(volume, "m_InternalProfile");
    profile_settings = field_of(profile, "settings");
    effect_active = field_of(effect, "active");
    cached_pointer = field_of(find_class("UnityEngine", "Object"), "m_CachedPtr");
    ready = manager_instance && manager_volumes && volume_shared && volume_internal &&
            profile_settings && effect_active && cached_pointer;
    pg3d_presentation_log(ready ? "optimizer effects: post-processing volume fields validated." :
                                  "optimizer effects unavailable: post-processing volume fields not found.");
}

static int option_for(void *effect) {
    void *klass = object_class(effect);
    const char *name = klass ? class_name(klass) : NULL;
    for (unsigned i = 0; name && i < sizeof(kinds) / sizeof(kinds[0]); ++i)
        if (strcmp(name, kinds[i].name) == 0) return kinds[i].option;
    return -1;
}
static const char *effect_name(void *effect) {
    void *klass = object_class(effect);
    const char *name = klass ? class_name(klass) : NULL;
    return name ? name : "?";
}
static bool get_active(void *effect) {
    bool active = false;
    field_value(effect, effect_active, &active);
    return active;
}
static void set_active(void *effect, bool active) { set_field_value(effect, effect_active, &active); }

static Tracked *find_tracked(void *effect, bool create) {
    Tracked *free_slot = NULL;
    for (unsigned i = 0; i < TRACK_LIMIT; ++i) {
        if (tracked[i].handle && handle_target(tracked[i].handle) == effect) return &tracked[i];
        if (!tracked[i].handle && !free_slot) free_slot = &tracked[i];
    }
    if (!create || !free_slot || !(free_slot->handle = handle_new(effect, false))) return NULL;
    free_slot->baseline = get_active(effect);
    return free_slot;
}
static void release(Tracked *t, bool restore) {
    void *effect = handle_target(t->handle);
    if (restore && alive(effect)) {
        set_active(effect, t->baseline);
        char line[160];
        snprintf(line, sizeof(line), "optimizer effect restored: %s active=%s.", effect_name(effect),
                 get_active(effect) ? "yes" : "no");
        pg3d_presentation_log(line);
    }
    handle_free(t->handle);
    t->handle = 0;
}

// Every effect in every profile the stack can currently blend.
static unsigned collect_effects(void **out, unsigned limit) {
    void *manager = NULL;
    static_field_value(manager_instance, &manager);
    if (!manager) return 0;
    void *list = NULL, *volumes[VOLUME_LIMIT];
    field_value(manager, manager_volumes, &list);
    unsigned volume_count = list_items(list, volumes, VOLUME_LIMIT), count = 0;
    for (unsigned v = 0; v < volume_count && count < limit; ++v) {
        if (!alive(volumes[v])) continue;
        void *profiles[2] = {NULL, NULL};
        field_value(volumes[v], volume_shared, &profiles[0]);
        field_value(volumes[v], volume_internal, &profiles[1]);
        for (unsigned p = 0; p < 2; ++p) {
            if (!alive(profiles[p]) || (p == 1 && profiles[1] == profiles[0])) continue;
            void *settings = NULL;
            field_value(profiles[p], profile_settings, &settings);
            count += list_items(settings, out + count, limit - count);
        }
    }
    return count;
}

bool pg3d_volumes_managing(void) {
    if (!ready) return false;
    for (unsigned i = 0; i < TRACK_LIMIT; ++i)
        if (tracked[i].handle) return true;
    return false;
}

void pg3d_volumes_apply(void) {
    unsigned found[OPT_COUNT] = {0}, active[OPT_COUNT] = {0}, managed[OPT_COUNT] = {0};
    if (ready) {
        for (unsigned i = 0; i < TRACK_LIMIT; ++i) {
            if (!tracked[i].handle) continue;
            void *effect = handle_target(tracked[i].handle);
            if (!alive(effect)) release(&tracked[i], false);
            else if (pg3d_options[tracked[i].option] != 0) release(&tracked[i], true);
        }
        void *effects[TRACK_LIMIT];
        unsigned count = collect_effects(effects, TRACK_LIMIT);
        for (unsigned i = 0; i < count; ++i) {
            if (!effects[i]) continue;
            int option = option_for(effects[i]);
            if (option < 0) continue;
            ++found[option];
            if (pg3d_options[option] == 0) {
                Tracked *t = find_tracked(effects[i], true);
                if (!t) continue;
                t->option = option;
                if (get_active(effects[i])) {
                    t->baseline = true; // the game switched it on: that is its value now
                    set_active(effects[i], false);
                    char line[160];
                    snprintf(line, sizeof(line), "optimizer effect off: %s (verified=%s).", effect_name(effects[i]),
                             get_active(effects[i]) ? "NO" : "yes");
                    pg3d_presentation_log(line);
                }
                if (t->baseline) ++managed[option];
            }
            if (get_active(effects[i])) ++active[option];
        }
    }
    // Legacy SSAO camera components count toward ambient occlusion.
    found[OPT_AMBIENT_OCCLUSION] += pg3d_ssao_found;
    active[OPT_AMBIENT_OCCLUSION] += pg3d_ssao_enabled;
    if (pg3d_options[OPT_AMBIENT_OCCLUSION] == 0 && pg3d_ssao_found > pg3d_ssao_enabled)
        managed[OPT_AMBIENT_OCCLUSION] += pg3d_ssao_found - pg3d_ssao_enabled;
    for (unsigned i = 0; i < sizeof(owned_options) / sizeof(owned_options[0]); ++i) {
        int option = owned_options[i];
        OptionStatus *o = &pg3d_option_status[option];
        o->state = !ready ? OPT_STATE_UNAVAILABLE : pg3d_options[option] != 0 ? OPT_STATE_GAME :
                   managed[option] ? OPT_STATE_APPLIED : OPT_STATE_ALREADY;
        o->game = found[option];
        o->current = active[option];
    }
    if (ready && pg3d_options[OPT_REFLECTIONS] == 0 && managed[OPT_REFLECTIONS]) {
        // Screen-space reflections off counts as applied even when probes were already off.
        if (pg3d_option_status[OPT_REFLECTIONS].state == OPT_STATE_ALREADY)
            pg3d_option_status[OPT_REFLECTIONS].state = OPT_STATE_APPLIED;
    }
}

// Per frame: keep tracked effects off if the game re-activated one.
void pg3d_volumes_frame(void) {
    if (!ready) return;
    for (unsigned i = 0; i < TRACK_LIMIT; ++i) {
        if (!tracked[i].handle) continue;
        void *effect = handle_target(tracked[i].handle);
        if (alive(effect) && get_active(effect)) {
            tracked[i].baseline = true;
            set_active(effect, false);
        }
    }
}
