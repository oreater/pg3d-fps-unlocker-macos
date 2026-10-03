// Target known camera image-effect components through IL2CPP's exported type
// and field APIs. No guessed RVAs, direct object offsets, or scene-object hiding.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "options.h"

typedef void *(*Resolver)(const char *);
extern void pg3d_presentation_log(const char *message);
static void *(*domain_get)(void);
static const void **(*domain_assemblies)(void *, size_t *);
static void *(*assembly_image)(const void *);
static void *(*class_from_name)(void *, const char *, const char *);
static const void *(*class_type)(void *);
static void *(*type_object)(const void *);
static void *(*class_field)(void *, const char *);
static void (*field_value)(void *, void *, void *);
static void (*static_field_value)(void *, void *);
static void *input_manager_field, *input_settings_field, *input_merging_field, *input_update_field;
static uint32_t (*handle_new)(void *, bool);
static void *(*handle_target)(uint32_t);
static void (*handle_free)(uint32_t);
static void *cached_pointer_field;
static void *(*main_camera)(void);
static void *(*get_game_object)(void *);
static void *(*get_component)(void *, void *);
static bool (*get_enabled)(void *);
static void (*set_enabled)(void *, bool);
static bool ready;
static bool (*get_occlusion)(void *);
static void (*set_occlusion)(void *, bool);
static uint32_t culling_camera_handle;
static bool culling_baseline;
static void *camera_class;
static int (*camera_count)(void);
static int (*fill_cameras)(void *);
static void *(*array_new)(void *, uintptr_t);
static uint32_t (*array_header_size)(void);
static uint32_t camera_array_handle;
static bool quiet_frame;
// Per effect type: disabled by the current options. Layer: post-processing
// off. Legacy SSAO: post-processing or ambient occlusion off.
static bool type_off[3], postfx_off, depth_off;
// Legacy SSAO components found / still enabled; volumes.c adds them to the
// ambient occlusion status.
unsigned pg3d_ssao_found, pg3d_ssao_enabled;
// Camera extras: depth-texture and HDR requests left behind on cameras whose
// post-processing we disabled. Restored to the observed values otherwise.
static int (*get_depth_mode)(void *);
static void (*set_depth_mode)(void *, int);
static bool (*get_hdr)(void *);
static void (*set_hdr)(void *, bool);
static void *(*get_target_texture)(void *);
static int (*get_rendering_path)(void *);
static float (*get_camera_depth)(void *);
static void *(*object_name)(void *);
static const uint16_t *(*string_chars)(void *);
static int32_t (*string_length)(void *);
static uint64_t fast_frames, rescan_frames, reasserted;
#define CAMERA_LIMIT 64
#define EFFECT_TYPES 3
#define EFFECT_LIMIT (CAMERA_LIMIT * EFFECT_TYPES)
typedef struct {
    const char *space, *name, *component_name;
    uint32_t type_handle, component_handle;
    int type;             // which of the EFFECT_TYPES this tracked slot holds
    bool baseline, owned, seen;
} Effect;
typedef struct {
    uint32_t camera_handle;
    int depth_baseline;
    bool hdr_baseline, owned;
    bool hdr;             // also keep HDR off: this camera's stack is off
} CameraExtra;
static CameraExtra camera_extras[CAMERA_LIMIT];
// Frame-guard cache: the active-camera list last fully scanned. Pointers are
// only compared, never dereferenced; the five-second tick always rescans.
static void *scanned_cameras[CAMERA_LIMIT];
static unsigned scanned_count;
static bool scanned_valid;
static void *audited_cameras[CAMERA_LIMIT];
static unsigned audited_count;
static Effect effects[EFFECT_LIMIT] = {
    {.space="UnityEngine.Rendering.PostProcessing", .name="PostProcessLayer"},
    {.space="", .name="SSAOEffect"},
    {.space="UnityStandardAssets.ImageEffects", .name="ScreenSpaceAmbientOcclusion"},
};

void pg3d_effects_bind(void *handle) {
#define BIND(variable, symbol) *(void **)(&variable) = dlsym(handle, symbol)
    BIND(domain_get, "il2cpp_domain_get");
    BIND(domain_assemblies, "il2cpp_domain_get_assemblies");
    BIND(assembly_image, "il2cpp_assembly_get_image");
    BIND(class_from_name, "il2cpp_class_from_name");
    BIND(class_type, "il2cpp_class_get_type");
    BIND(type_object, "il2cpp_type_get_object");
    BIND(class_field, "il2cpp_class_get_field_from_name");
    BIND(field_value, "il2cpp_field_get_value");
    BIND(static_field_value, "il2cpp_field_static_get_value");
    BIND(handle_new, "il2cpp_gchandle_new");
    BIND(handle_target, "il2cpp_gchandle_get_target");
    BIND(handle_free, "il2cpp_gchandle_free");
    BIND(array_new, "il2cpp_array_new");
    BIND(array_header_size, "il2cpp_array_object_header_size");
    BIND(string_chars, "il2cpp_string_chars");
    BIND(string_length, "il2cpp_string_length");
#undef BIND
}
static void *find_class(const char *space, const char *name) {
    size_t count = 0;
    const void **assemblies = domain_assemblies(domain_get(), &count);
    if (count > 4096) return NULL;
    for (size_t i=0; i<count; ++i) {
        void *klass = class_from_name(assembly_image(assemblies[i]), space, name);
        if (klass) return klass;
    }
    return NULL;
}
static bool alive(void *object) {
    if (!object) return false;
    void *native = NULL;
    field_value(object, cached_pointer_field, &native);
    return native != NULL;
}
// Read the already-existing manager without invoking InputSystem's getters or
// changing input settings. Metadata fields avoid build-specific object offsets.
static void audit_input_settings(void) {
    if (!static_field_value || !input_manager_field || !input_settings_field ||
        !input_merging_field || !input_update_field) return;
    void *manager = NULL, *settings = NULL;
    static_field_value(input_manager_field, &manager);
    if (manager) field_value(manager, input_settings_field, &settings);
    if (!settings) return;
    bool disabled = false;
    int mode = 0;
    field_value(settings, input_merging_field, &disabled);
    field_value(settings, input_update_field, &mode);
    char line[220];
    snprintf(line, sizeof(line), "input settings audit: redundant_event_merging=%s; update_mode=%d; read_only=yes. Does not prove this is the camera input path.",
             disabled ? "off" : "on", mode);
    pg3d_presentation_log(line);
}
static void release_component(Effect *effect) {
    if (!effect->component_handle) return;
    void *object = handle_target(effect->component_handle);
    // A strong GC handle preserves the managed shell, but Unity may have
    // destroyed its native component during a scene transition.
    if (effect->owned && alive(object)) {
        set_enabled(object, effect->baseline);
        char line[180];
        snprintf(line, sizeof(line), "optimizer postfx restore: %s; enabled=%s; verified=%s.",
                 effect->component_name, effect->baseline ? "yes" : "no",
                 get_enabled(object) == effect->baseline ? "yes" : "NO");
        if (!quiet_frame) pg3d_presentation_log(line);
    }
    handle_free(effect->component_handle);
    effect->component_handle = 0;
    effect->owned = false;
}
void pg3d_effects_start(Resolver resolve) {
    if (!domain_get || !domain_assemblies || !assembly_image || !class_from_name ||
        !class_type || !type_object || !class_field || !field_value ||
        !handle_new || !handle_target || !handle_free) {
        pg3d_presentation_log("optimizer postfx unavailable: missing IL2CPP exports.");
        return;
    }
    main_camera = (void *(*)(void))resolve("UnityEngine.Camera::get_main");
    camera_count = (int (*)(void))resolve("UnityEngine.Camera::GetAllCamerasCount");
    fill_cameras = (int (*)(void *))resolve("UnityEngine.Camera::GetAllCamerasImpl");
    camera_class = find_class("UnityEngine", "Camera");
    get_game_object = (void *(*)(void *))resolve("UnityEngine.Component::get_gameObject");
    // Component::GetComponent is the STRING overload in this build. The Type
    // overload forwards to GameObject::GetComponent; mixing these ABIs crashes.
    get_component = (void *(*)(void *, void *))resolve("UnityEngine.GameObject::GetComponent");
    get_occlusion = (bool (*)(void *))resolve("UnityEngine.Camera::get_useOcclusionCulling");
    set_occlusion = (void (*)(void *, bool))resolve("UnityEngine.Camera::set_useOcclusionCulling");
    get_enabled = (bool (*)(void *))resolve("UnityEngine.Behaviour::get_enabled");
    get_depth_mode = (int (*)(void *))resolve("UnityEngine.Camera::get_depthTextureMode");
    set_depth_mode = (void (*)(void *, int))resolve("UnityEngine.Camera::set_depthTextureMode");
    get_hdr = (bool (*)(void *))resolve("UnityEngine.Camera::get_allowHDR");
    set_hdr = (void (*)(void *, bool))resolve("UnityEngine.Camera::set_allowHDR");
    get_target_texture = (void *(*)(void *))resolve("UnityEngine.Camera::get_targetTexture");
    get_rendering_path = (int (*)(void *))resolve("UnityEngine.Camera::get_actualRenderingPath");
    get_camera_depth = (float (*)(void *))resolve("UnityEngine.Camera::get_depth");
    object_name = (void *(*)(void *))resolve("UnityEngine.Object::GetName");
    set_enabled = (void (*)(void *, bool))resolve("UnityEngine.Behaviour::set_enabled");
    void *object_class = find_class("UnityEngine", "Object");
    cached_pointer_field = object_class ? class_field(object_class, "m_CachedPtr") : NULL;
    void *input_system = find_class("UnityEngine.InputSystem", "InputSystem");
    void *input_manager = find_class("UnityEngine.InputSystem", "InputManager");
    void *input_settings = find_class("UnityEngine.InputSystem", "InputSettings");
    input_manager_field = input_system ? class_field(input_system, "s_Manager") : NULL;
    input_settings_field = input_manager ? class_field(input_manager, "m_Settings") : NULL;
    input_merging_field = input_settings ? class_field(input_settings, "m_DisableRedundantEventsMerging") : NULL;
    input_update_field = input_settings ? class_field(input_settings, "m_UpdateMode") : NULL;
    if (!main_camera || !get_game_object || !get_component || !get_enabled || !set_enabled || !cached_pointer_field) {
        pg3d_presentation_log("optimizer postfx unavailable: missing validated component bindings or lifetime field.");
        return;
    }
    for (size_t i=0; i<EFFECT_TYPES; ++i) {
        Effect *e = &effects[i];
        void *klass = find_class(e->space, e->name);
        void *reflection_type = klass ? type_object(class_type(klass)) : NULL;
        if (reflection_type) e->type_handle = handle_new(reflection_type, false);
    }
    ready = true;
}
static void apply_effects(void);
static unsigned collect_cameras(void **cameras, bool *enumerated);
static bool extras_supported(void) {
    return get_depth_mode && set_depth_mode && get_hdr && set_hdr && get_target_texture;
}
static bool any_type_off(void) { return type_off[0] || type_off[1] || type_off[2]; }
bool pg3d_effects_managing(void) { return ready && (any_type_off() || (depth_off && extras_supported())); }
static CameraExtra *extra_for(void *camera, bool create) {
    CameraExtra *free_slot = NULL;
    for (unsigned i=0; i<CAMERA_LIMIT; ++i) {
        CameraExtra *x = &camera_extras[i];
        if (x->camera_handle && handle_target(x->camera_handle) == camera) return x;
        if (!x->camera_handle && !free_slot) free_slot = x;
    }
    if (!create || !free_slot) return NULL;
    free_slot->camera_handle = handle_new(camera, false);
    if (!free_slot->camera_handle) return NULL;
    free_slot->depth_baseline = get_depth_mode(camera);
    free_slot->hdr_baseline = get_hdr(camera);
    free_slot->hdr = false;
    free_slot->owned = true;
    return free_slot;
}
// Keep one camera's depth texture (and, when its stack is off, HDR) requests
// off; returns true if a value had to be re-applied (for example after the
// game reset the camera).
static bool enforce_extra(CameraExtra *x, void *camera) {
    bool changed = false;
    int depth = get_depth_mode(camera);
    if (depth != 0) {
        x->depth_baseline |= depth;
        set_depth_mode(camera, 0);
        changed = true;
    }
    if (x->hdr && get_hdr(camera)) {
        x->hdr_baseline = true;
        set_hdr(camera, false);
        changed = true;
    }
    return changed;
}
static void release_extra(CameraExtra *x, bool restore) {
    if (!x->camera_handle) return;
    void *camera = handle_target(x->camera_handle);
    if (restore && x->owned && alive(camera)) {
        set_depth_mode(camera, x->depth_baseline);
        if (x->hdr) set_hdr(camera, x->hdr_baseline);
        char line[200];
        snprintf(line, sizeof(line), "optimizer camera extras restore: depthTextureMode=%d; allowHDR=%s; verified=%s.",
                 x->depth_baseline, x->hdr ? (x->hdr_baseline ? "yes" : "no") : "unchanged",
                 get_depth_mode(camera) == x->depth_baseline && (!x->hdr || get_hdr(camera) == x->hdr_baseline) ? "yes" : "NO");
        // Cameras that never had a depth pass (UI cameras) restore silently.
        if (!quiet_frame && (x->depth_baseline || x->hdr)) pg3d_presentation_log(line);
    }
    handle_free(x->camera_handle);
    x->camera_handle = 0;
    x->owned = false;
    x->hdr = false;
}
// Steady state: the same cameras as the last full scan. Re-disable tracked
// effects and camera extras without GetComponent lookups.
static void enforce_tracked(void) {
    for (unsigned j=0; j<EFFECT_LIMIT; ++j) {
        Effect *e = &effects[j];
        if (!e->component_handle || !e->owned) continue;
        void *object = handle_target(e->component_handle);
        if (!alive(object) || !get_enabled(object)) continue;
        e->baseline = true; // An observed game reset becomes the restoration baseline.
        set_enabled(object, false);
        ++reasserted;
    }
    if (!extras_supported()) return;
    for (unsigned i=0; i<CAMERA_LIMIT; ++i) {
        CameraExtra *x = &camera_extras[i];
        if (!x->camera_handle || !x->owned) continue;
        void *camera = handle_target(x->camera_handle);
        if (alive(camera) && enforce_extra(x, camera)) ++reasserted;
    }
}
void pg3d_effects_frame(void) {
    if (!pg3d_effects_managing()) return;
    quiet_frame = true;
    void *cameras[CAMERA_LIMIT] = {0};
    bool enumerated = false;
    unsigned count = collect_cameras(cameras, &enumerated);
    if (scanned_valid && enumerated && count == scanned_count &&
        memcmp(cameras, scanned_cameras, count * sizeof(void *)) == 0) {
        enforce_tracked();
        ++fast_frames;
    } else {
        apply_effects();
        ++rescan_frames;
    }
    quiet_frame = false;
}
static void camera_name(void *camera, char *out, size_t size) {
    snprintf(out, size, "?");
    void *game_object = get_game_object ? get_game_object(camera) : NULL;
    void *name = object_name && game_object ? object_name(game_object) : NULL;
    if (!name || !string_chars || !string_length) return;
    const uint16_t *chars = string_chars(name);
    int32_t length = string_length(name);
    size_t n = 0;
    for (int32_t i=0; chars && i<length && n+1<size; ++i)
        out[n++] = chars[i] >= 0x20 && chars[i] < 0x7f && chars[i] != ';' ? (char)chars[i] : '?';
    out[n] = 0;
}
// Read-only description of each active camera, logged when the set changes.
static void audit_cameras(void **cameras, unsigned count) {
    if (count == audited_count && memcmp(cameras, audited_cameras, count * sizeof(void *)) == 0) return;
    memcpy(audited_cameras, cameras, count * sizeof(void *));
    audited_count = count;
    for (unsigned c=0; c<count; ++c) {
        if (!alive(cameras[c])) continue;
        char name[64], line[320];
        camera_name(cameras[c], name, sizeof(name));
        snprintf(line, sizeof(line), "camera audit %u/%u: name=%s; depth=%.1f; target_texture=%s; path=%d; depthTextureMode=%d; allowHDR=%s.",
                 c + 1, count, name, get_camera_depth ? get_camera_depth(cameras[c]) : 0.0f,
                 get_target_texture ? (get_target_texture(cameras[c]) ? "yes" : "no") : "?",
                 get_rendering_path ? get_rendering_path(cameras[c]) : -2,
                 get_depth_mode ? get_depth_mode(cameras[c]) : -1,
                 get_hdr ? (get_hdr(cameras[c]) ? "yes" : "no") : "?");
        pg3d_presentation_log(line);
    }
}
static bool read_mode_file(const char *control, const char *suffix, const char *wanted) {
    char path[PATH_MAX], mode[32], extra;
    int n = snprintf(path, sizeof(path), "%s%s", control, suffix);
    FILE *file = n > 0 && (size_t)n < sizeof(path) ? fopen(path, "r") : NULL;
    if (!file) return false;
    bool match = fscanf(file, "%31s %c", mode, &extra) == 1 && strcmp(mode, wanted) == 0;
    fclose(file);
    return match;
}
void pg3d_effects_tick(void) {
    OptionStatus *postfx = &pg3d_option_status[OPT_POSTFX], *depth = &pg3d_option_status[OPT_DEPTH_PASS];
    if (!ready) {
        postfx->state = depth->state = OPT_STATE_UNAVAILABLE;
        return;
    }
    audit_input_settings();
    postfx_off = pg3d_options[OPT_POSTFX] == 0;
    depth_off = pg3d_options[OPT_DEPTH_PASS] == 0;
    type_off[0] = postfx_off;
    type_off[1] = type_off[2] = postfx_off || pg3d_options[OPT_AMBIENT_OCCLUSION] == 0;
    const char *control = getenv("PG3D_OPT_CONTROL");
    void *camera = main_camera();
    if (get_occlusion && set_occlusion) {
        bool culling_off = control && read_mode_file(control, ".occlusion", "off");
        void *previous = culling_camera_handle ? handle_target(culling_camera_handle) : NULL;
        if (culling_camera_handle && (!culling_off || previous != camera)) {
            if (alive(previous)) set_occlusion(previous, culling_baseline);
            handle_free(culling_camera_handle); culling_camera_handle = 0;
        }
        if (camera && culling_off) {
            if (!culling_camera_handle) {
                culling_baseline = get_occlusion(camera);
                culling_camera_handle = handle_new(camera, false);
            }
            if (culling_camera_handle) set_occlusion(camera, false);
        }
        char line[240];
        snprintf(line, sizeof(line), "optimizer culling diagnostic: requested=%s; camera=%s; occlusion=%s. Disabling occlusion can increase GPU work; frustum culling is unchanged.",
                 culling_off ? "off" : "game", camera ? "yes" : "no",
                 camera ? (get_occlusion(camera) ? "on" : "off") : "unknown");
        pg3d_presentation_log(line);
    }
    apply_effects();
    char line[200];
    snprintf(line, sizeof(line), "optimizer frame cache: fast=%llu; rescans=%llu; reasserted=%llu.",
             (unsigned long long)fast_frames, (unsigned long long)rescan_frames, (unsigned long long)reasserted);
    pg3d_presentation_log(line);
    fast_frames = rescan_frames = reasserted = 0;
}
static unsigned collect_cameras(void **cameras, bool *enumerated_out) {
    void *camera = main_camera();
    // Enumerate active cameras, including untagged gameplay/weapon cameras.
    // Array storage offset comes from the runtime itself, never a guessed ABI.
    unsigned count = camera ? 1 : 0;
    cameras[0] = camera;
    bool enumerated = false;
    if (camera_count && fill_cameras && camera_class && array_new && array_header_size) {
        int wanted = camera_count();
        uint32_t header = array_header_size();
        if (wanted > 0 && wanted <= CAMERA_LIMIT && header >= 2*sizeof(void *) &&
            header <= 128 && header % sizeof(void *) == 0) {
            if (!camera_array_handle) {
                void *array = array_new(camera_class, CAMERA_LIMIT);
                if (array) camera_array_handle = handle_new(array, false);
            }
            void *array = camera_array_handle ? handle_target(camera_array_handle) : NULL;
            if (array) {
                int filled = fill_cameras(array);
                if (filled >= 0 && filled <= wanted) {
                    memcpy(cameras, (char *)array + header, (size_t)filled * sizeof(void *));
                    count = (unsigned)filled;
                    enumerated = true;
                }
            }
        }
    }
    *enumerated_out = enumerated;
    return count;
}
static void apply_effects(void) {
    void *cameras[CAMERA_LIMIT] = {0};
    bool enumerated = false;
    unsigned count = collect_cameras(cameras, &enumerated);
    if (!quiet_frame) audit_cameras(cameras, count);
    bool extras_on = (postfx_off || depth_off) && extras_supported();
    for (unsigned i=0; i<EFFECT_LIMIT; ++i) {
        effects[i].seen = false;
        if (effects[i].component_handle &&
            (!type_off[effects[i].type] || !alive(handle_target(effects[i].component_handle))))
            release_component(&effects[i]);
    }
    for (unsigned i=0; i<CAMERA_LIMIT; ++i) {
        CameraExtra *x = &camera_extras[i];
        if (!x->camera_handle) continue;
        void *camera = handle_target(x->camera_handle);
        // Restore when the option that asked for it is back on; a destroyed
        // camera is only forgotten.
        if (!alive(camera)) release_extra(x, false);
        else if (!extras_on || (!depth_off && !x->hdr)) release_extra(x, true);
    }
    unsigned found[EFFECT_TYPES]={0}, enabled[EFFECT_TYPES]={0}, extras=0;
    for (unsigned c=0; c<count; ++c) {
      bool owns_effect = false;
      void *game_object = alive(cameras[c]) ? get_game_object(cameras[c]) : NULL;
      for (unsigned i=0; i<EFFECT_TYPES; ++i) {
        void *type = effects[i].type_handle ? handle_target(effects[i].type_handle) : NULL;
        void *object = game_object && type ? get_component(game_object, type) : NULL;
        if (!alive(object)) continue;
        ++found[i];
        if (!type_off[i]) {
            if (get_enabled(object)) ++enabled[i];
            continue;
        }
        Effect *e = NULL;
        for (unsigned j=0; j<EFFECT_LIMIT; ++j) {
            if (effects[j].component_handle && handle_target(effects[j].component_handle) == object) {
                e = &effects[j]; break;
            }
        }
        if (!e) {
            for (unsigned j=0; j<EFFECT_LIMIT; ++j) {
                if (!effects[j].component_handle) { e = &effects[j]; break; }
            }
        }
        if (!e) continue;
        e->seen = true;
        // Names for the three type definitions must remain stable even when a
        // tracking slot is reused for a different effect type.
        bool current = get_enabled(object);
        if (!e->component_handle) {
            e->component_handle = handle_new(object, false);
            e->baseline = current;
            e->component_name = effects[i].name;
            e->type = (int)i;
        }
        if (!e->component_handle) continue;
        // An observed game reset to enabled becomes the restoration baseline.
        if (e->owned && current) e->baseline = true;
        if (current) {
            set_enabled(object, false);
            char line[200];
            snprintf(line, sizeof(line), "optimizer postfx applied: %s; enabled=no; verified=%s; active camera.",
                     effects[i].name, !get_enabled(object) ? "yes" : "NO");
            if (!quiet_frame) pg3d_presentation_log(line);
        }
        e->owned = true;
        owns_effect = true;
        if (get_enabled(object)) ++enabled[i];
      }
      // Post-processing off: only cameras whose stack we disabled, since that
      // stack is what requested the extra depth pass and the HDR target.
      // Depth pre-pass off: every screen camera, HDR untouched.
      bool stack_off = postfx_off && owns_effect;
      if (extras_on && (stack_off || depth_off) && !get_target_texture(cameras[c])) {
        CameraExtra *x = extra_for(cameras[c], true);
        if (x) {
            if (x->hdr && !stack_off) set_hdr(cameras[c], x->hdr_baseline); // stack back on
            x->hdr = stack_off;
            enforce_extra(x, cameras[c]);
            if (get_depth_mode(cameras[c]) == 0) ++extras;
        }
      }
    }
    bool managing = any_type_off() || extras_on;
    if (managing) {
        memcpy(scanned_cameras, cameras, count * sizeof(void *));
        scanned_count = count;
        scanned_valid = enumerated;
    } else scanned_valid = false;
    pg3d_ssao_found = found[1] + found[2];
    pg3d_ssao_enabled = enabled[1] + enabled[2];
    OptionStatus *postfx = &pg3d_option_status[OPT_POSTFX], *depth = &pg3d_option_status[OPT_DEPTH_PASS];
    postfx->state = !postfx_off ? OPT_STATE_GAME : found[0] == 0 ? OPT_STATE_ALREADY : OPT_STATE_APPLIED;
    postfx->game = found[0];
    postfx->current = enabled[0];
    unsigned depth_owned = 0;
    for (unsigned i=0; i<CAMERA_LIMIT; ++i)
        if (camera_extras[i].camera_handle && camera_extras[i].owned && camera_extras[i].depth_baseline) ++depth_owned;
    depth->state = !extras_supported() ? OPT_STATE_UNAVAILABLE : !depth_off ? OPT_STATE_GAME :
                   depth_owned ? OPT_STATE_APPLIED : OPT_STATE_ALREADY;
    depth->game = depth_owned;
    depth->current = 0;
    // A death camera or pooled player camera can disappear temporarily. Keep
    // its original state and the override until explicit restoration or
    // native destruction; inactivity is not permission to re-enable it.
    char line[300];
    snprintf(line, sizeof(line), "optimizer postfx status: cameras=%u; scope=%s; found=%u; disabled=%u; ssao=%u/%u; policy=%s; depth_pass=%s; camera_extras=%s:%u. Baked lighting and custom shadow meshes are unaffected.",
             count, enumerated ? "active" : "main_fallback", found[0], found[0] - enabled[0],
             pg3d_ssao_found - pg3d_ssao_enabled, pg3d_ssao_found, postfx_off ? "off" : "game",
             depth_off ? "off" : "game", !extras_supported() ? "unavailable" : extras_on ? "on" : "off", extras);
    if (!quiet_frame) pg3d_presentation_log(line);
}
