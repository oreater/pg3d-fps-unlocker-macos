// Read-only inventory of what the current scene renders with: engine and
// quality settings, cameras and the behaviours on them, post-processing
// volumes and their effects, lights, reflection probes and renderers. Written
// to the session log on request (`--probe`) so options can be judged against
// what the game actually uses. Nothing here changes a value.
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *(*Resolver)(const char *);
extern void pg3d_presentation_log(const char *message);

#define LIMIT 20000
static void *items[LIMIT];
static Resolver resolve;
static void *(*domain_get)(void);
static const void **(*domain_assemblies)(void *, size_t *);
static void *(*assembly_image)(const void *);
static void *(*class_from_name)(void *, const char *, const char *);
static const void *(*class_type)(void *);
static void *(*type_object)(const void *);
static void *(*class_field)(void *, const char *);
static void (*field_value)(void *, void *, void *);
static void *(*object_class)(void *);
static const char *(*class_name)(void *);
static const char *(*class_namespace)(void *);
static uint32_t (*array_header_size)(void);
static uint32_t (*array_length)(void *);
static const uint16_t *(*string_chars)(void *);
static int32_t (*string_length)(void *);
static void *cached_pointer;

void pg3d_probe_bind(void *handle) {
#define BIND(variable, symbol) *(void **)(&variable) = dlsym(handle, symbol)
    BIND(domain_get, "il2cpp_domain_get");
    BIND(domain_assemblies, "il2cpp_domain_get_assemblies");
    BIND(assembly_image, "il2cpp_assembly_get_image");
    BIND(class_from_name, "il2cpp_class_from_name");
    BIND(class_type, "il2cpp_class_get_type");
    BIND(type_object, "il2cpp_type_get_object");
    BIND(class_field, "il2cpp_class_get_field_from_name");
    BIND(field_value, "il2cpp_field_get_value");
    BIND(object_class, "il2cpp_object_get_class");
    BIND(class_name, "il2cpp_class_get_name");
    BIND(class_namespace, "il2cpp_class_get_namespace");
    BIND(array_header_size, "il2cpp_array_object_header_size");
    BIND(array_length, "il2cpp_array_length");
    BIND(string_chars, "il2cpp_string_chars");
    BIND(string_length, "il2cpp_string_length");
#undef BIND
}

static void log_line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *format, ...) {
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    pg3d_presentation_log(line);
}

// Bindings resolved by name each time (the probe is rare); a missing one
// reads as zero so an unexpected Unity build can never call a null pointer.
static void *bind(const char *name) { return resolve ? resolve(name) : NULL; }
static int int_of(const char *name, void *object) {
    int (*f)(void *) = (int (*)(void *))bind(name); return f && object ? f(object) : -1;
}
static float float_of(const char *name, void *object) {
    float (*f)(void *) = (float (*)(void *))bind(name); return f && object ? f(object) : -1;
}
static bool bool_of(const char *name, void *object) {
    bool (*f)(void *) = (bool (*)(void *))bind(name); return f && object ? f(object) : false;
}
static void *pointer_of(const char *name, void *object) {
    void *(*f)(void *) = (void *(*)(void *))bind(name); return f && object ? f(object) : NULL;
}
static int int_global(const char *name) { int (*f)(void) = (int (*)(void))bind(name); return f ? f() : -1; }
static float float_global(const char *name) { float (*f)(void) = (float (*)(void))bind(name); return f ? f() : -1; }
static void *pointer_global(const char *name) { void *(*f)(void) = (void *(*)(void))bind(name); return f ? f() : NULL; }

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
static bool alive(void *object) {
    if (!object || !cached_pointer) return false;
    void *native = NULL;
    field_value(object, cached_pointer, &native);
    return native != NULL;
}
static void managed_string(void *string, char *out, size_t size) {
    size_t n = 0;
    if (string && size) {
        const uint16_t *chars = string_chars(string);
        int32_t length = string_length(string);
        for (int32_t i = 0; chars && i < length && n + 1 < size; ++i)
            out[n++] = chars[i] >= 0x20 && chars[i] < 0x7f ? (char)chars[i] : '?';
    }
    if (size) out[n] = 0;
}
static void object_name(void *object, char *out, size_t size) {
    snprintf(out, size, "?");
    if (alive(object)) managed_string(pointer_of("UnityEngine.Object::GetName", object), out, size);
}
static unsigned array_items(void *array, void **out, unsigned limit) {
    if (!array) return 0;
    uint32_t header = array_header_size();
    if (header < 2 * sizeof(void *) || header > 128 || header % sizeof(void *)) return 0;
    uint32_t length = array_length(array);
    unsigned count = length < limit ? length : limit;
    memcpy(out, (char *)array + header, count * sizeof(void *));
    return count;
}
static bool field(void *object, const char *name, void *out) {
    void *klass = object ? object_class(object) : NULL;
    void *f = klass ? class_field(klass, name) : NULL;
    if (!f) return false;
    field_value(object, f, out);
    return true;
}
static unsigned objects_of(const char *space, const char *name, bool inactive) {
    void *klass = find_class(space, name);
    void *type = klass ? type_object(class_type(klass)) : NULL;
    void *(*find)(void *, bool) = (void *(*)(void *, bool))bind("UnityEngine.Object::FindObjectsOfType");
    if (!type || !find) return 0;
    return array_items(find(type, inactive), items, LIMIT);
}
static void class_label(void *object, char *out, size_t size) {
    void *klass = object_class(object);
    const char *space = klass ? class_namespace(klass) : "";
    snprintf(out, size, "%s%s%s", space && *space ? space : "", space && *space ? "." : "",
             klass ? class_name(klass) : "?");
}

static void dump_profile(const char *label, void *profile) {
    if (!alive(profile)) return;
    char name[96];
    object_name(profile, name, sizeof(name));
    void *list = NULL, *array = NULL;
    int32_t size = 0;
    if (!field(profile, "settings", &list) || !list || !field(list, "_items", &array) || !field(list, "_size", &size)) {
        log_line("probe:   %s profile %s: settings list unreadable", label, name);
        return;
    }
    void *effects[64];
    unsigned count = array_items(array, effects, size > 0 && size < 64 ? (unsigned)size : 0);
    char line[900];
    int used = snprintf(line, sizeof(line), "probe:   %s profile %s (%u effects):", label, name, count);
    for (unsigned i = 0; i < count && used > 0 && (size_t)used < sizeof(line) - 80; ++i) {
        bool active = false, enabled = false;
        void *parameter = NULL;
        field(effects[i], "active", &active);
        if (field(effects[i], "enabled", &parameter) && parameter) field(parameter, "value", &enabled);
        void *klass = object_class(effects[i]);
        used += snprintf(line + used, sizeof(line) - used, " %s[active=%d enabled=%d]",
                         klass ? class_name(klass) : "?", active, enabled);
    }
    log_line("%s", line);
}

static void dump_engine(void) {
    char device[96] = "?", version[96] = "?", unity[32] = "?";
    managed_string(pointer_global("UnityEngine.SystemInfo::GetGraphicsDeviceName"), device, sizeof(device));
    managed_string(pointer_global("UnityEngine.SystemInfo::GetGraphicsDeviceVersion"), version, sizeof(version));
    managed_string(pointer_global("UnityEngine.Application::get_unityVersion"), unity, sizeof(unity));
    log_line("probe: engine: unity=%s device=%s (%s); rendering_threading=%d; job_workers=%d; screen=%dx%d; quality_level=%d",
             unity, device, version, int_global("UnityEngine.SystemInfo::GetRenderingThreadingMode"),
             int_global("Unity.Jobs.LowLevel.Unsafe.JobsUtility::GetJobQueueWorkerThreadCount"),
             int_global("UnityEngine.Screen::get_width"), int_global("UnityEngine.Screen::get_height"),
             int_global("UnityEngine.QualitySettings::GetQualityLevel"));
    log_line("probe: quality: shadows=%d distance=%.0f cascades=%d resolution=%d msaa=%d aniso=%d pixel_lights=%d lod_bias=%.2f max_lod=%d texture_limit=%d skin_weights=%d particle_raycasts=%d soft_particles=%d soft_vegetation=%d realtime_reflections=%d",
             int_global("UnityEngine.QualitySettings::get_shadows"), float_global("UnityEngine.QualitySettings::get_shadowDistance"),
             int_global("UnityEngine.QualitySettings::get_shadowCascades"), int_global("UnityEngine.QualitySettings::get_shadowResolution"),
             int_global("UnityEngine.QualitySettings::get_antiAliasing"), int_global("UnityEngine.QualitySettings::get_anisotropicFiltering"),
             int_global("UnityEngine.QualitySettings::get_pixelLightCount"), float_global("UnityEngine.QualitySettings::get_lodBias"),
             int_global("UnityEngine.QualitySettings::get_maximumLODLevel"), int_global("UnityEngine.QualitySettings::get_masterTextureLimit"),
             int_global("UnityEngine.QualitySettings::get_skinWeights"), int_global("UnityEngine.QualitySettings::get_particleRaycastBudget"),
             int_global("UnityEngine.QualitySettings::get_softParticles") & 1, int_global("UnityEngine.QualitySettings::get_softVegetation") & 1,
             int_global("UnityEngine.QualitySettings::get_realtimeReflectionProbes") & 1);
    log_line("probe: render settings: fog=%d mode=%d density=%.4f start=%.0f end=%.0f ambient_mode=%d",
             int_global("UnityEngine.RenderSettings::get_fog") & 1, int_global("UnityEngine.RenderSettings::get_fogMode"),
             float_global("UnityEngine.RenderSettings::get_fogDensity"), float_global("UnityEngine.RenderSettings::get_fogStartDistance"),
             float_global("UnityEngine.RenderSettings::get_fogEndDistance"), int_global("UnityEngine.RenderSettings::get_ambientMode"));
}

static void dump_cameras(void) {
    unsigned count = objects_of("UnityEngine", "Camera", false);
    log_line("probe: cameras: %u enabled", count);
    void *cameras[64];
    unsigned kept = count < 64 ? count : 64;
    memcpy(cameras, items, kept * sizeof(void *));
    for (unsigned c = 0; c < kept; ++c) {
        void *camera = cameras[c];
        if (!alive(camera)) continue;
        char name[80];
        object_name(pointer_of("UnityEngine.Component::get_gameObject", camera), name, sizeof(name));
        log_line("probe:   camera %s: enabled=%d depth=%.1f hdr=%d msaa=%d depth_texture=%d path=%d target=%s far=%.0f occlusion=%d mask=0x%x",
                 name, bool_of("UnityEngine.Behaviour::get_enabled", camera), float_of("UnityEngine.Camera::get_depth", camera),
                 bool_of("UnityEngine.Camera::get_allowHDR", camera), bool_of("UnityEngine.Camera::get_allowMSAA", camera),
                 int_of("UnityEngine.Camera::get_depthTextureMode", camera), int_of("UnityEngine.Camera::get_actualRenderingPath", camera),
                 pointer_of("UnityEngine.Camera::get_targetTexture", camera) ? "texture" : "screen",
                 float_of("UnityEngine.Camera::get_farClipPlane", camera), bool_of("UnityEngine.Camera::get_useOcclusionCulling", camera),
                 (unsigned)int_of("UnityEngine.Camera::get_cullingMask", camera));
    }
    // Behaviours on camera GameObjects: image effects and similar scripts.
    void *camera_type = NULL;
    void *camera_class = find_class("UnityEngine", "Camera");
    if (camera_class) camera_type = type_object(class_type(camera_class));
    void *(*get_component)(void *, void *) = (void *(*)(void *, void *))bind("UnityEngine.GameObject::GetComponent");
    unsigned behaviours = objects_of("UnityEngine", "MonoBehaviour", false), logged = 0;
    for (unsigned i = 0; i < behaviours && logged < 60 && camera_type && get_component; ++i) {
        void *game_object = alive(items[i]) ? pointer_of("UnityEngine.Component::get_gameObject", items[i]) : NULL;
        if (!game_object || !alive(get_component(game_object, camera_type))) continue;
        char klass[96], name[80];
        class_label(items[i], klass, sizeof(klass));
        object_name(game_object, name, sizeof(name));
        log_line("probe:   camera behaviour on %s: %s enabled=%d", name, klass, bool_of("UnityEngine.Behaviour::get_enabled", items[i]));
        ++logged;
    }
    log_line("probe: behaviours in scene: %u", behaviours);
}

static void dump_volumes(void) {
    const char *space = "UnityEngine.Rendering.PostProcessing";
    unsigned count = objects_of(space, "PostProcessLayer", true);
    for (unsigned i = 0; i < count && i < 8; ++i) {
        char name[80];
        object_name(pointer_of("UnityEngine.Component::get_gameObject", items[i]), name, sizeof(name));
        int antialiasing = -1;
        void *fog = NULL;
        bool deferred_fog = false;
        field(items[i], "antialiasingMode", &antialiasing);
        if (field(items[i], "fog", &fog) && fog) field(fog, "enabled", &deferred_fog);
        log_line("probe: post-processing layer on %s: enabled=%d antialiasing=%d deferred_fog=%d", name,
                 bool_of("UnityEngine.Behaviour::get_enabled", items[i]), antialiasing, deferred_fog);
    }
    count = objects_of(space, "PostProcessVolume", true);
    log_line("probe: post-processing volumes: %u", count);
    void *volumes[24];
    unsigned kept = count < 24 ? count : 24;
    memcpy(volumes, items, kept * sizeof(void *));
    for (unsigned i = 0; i < kept; ++i) {
        bool global = false;
        float weight = 0, priority = 0;
        void *shared = NULL, *internal = NULL;
        field(volumes[i], "isGlobal", &global);
        field(volumes[i], "weight", &weight);
        field(volumes[i], "priority", &priority);
        field(volumes[i], "sharedProfile", &shared);
        field(volumes[i], "m_InternalProfile", &internal);
        char name[80];
        object_name(pointer_of("UnityEngine.Component::get_gameObject", volumes[i]), name, sizeof(name));
        log_line("probe:  volume %s: enabled=%d global=%d weight=%.2f priority=%.1f", name,
                 bool_of("UnityEngine.Behaviour::get_enabled", volumes[i]), global, weight, priority);
        dump_profile("shared", shared);
        if (internal && internal != shared) dump_profile("instance", internal);
    }
}

static void dump_lights(void) {
    unsigned count = objects_of("UnityEngine", "Light", false);
    unsigned by_type[5] = {0}, shadowed = 0, pixel = 0, vertex = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!alive(items[i])) continue;
        int type = int_of("UnityEngine.Light::get_type", items[i]);
        if (type >= 0 && type < 5) ++by_type[type];
        if (int_of("UnityEngine.Light::get_shadows", items[i]) > 0) ++shadowed;
        int mode = int_of("UnityEngine.Light::get_renderMode", items[i]);
        if (mode == 1) ++pixel; else if (mode == 2) ++vertex;
    }
    log_line("probe: lights: %u (spot %u directional %u point %u area %u); with shadows %u; forced pixel %u vertex %u",
             count, by_type[0], by_type[1], by_type[2], by_type[3], shadowed, pixel, vertex);
    count = objects_of("UnityEngine", "ReflectionProbe", false);
    unsigned realtime = 0, every_frame = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!alive(items[i])) continue;
        if (int_of("UnityEngine.ReflectionProbe::get_mode", items[i]) == 1) ++realtime;
        if (int_of("UnityEngine.ReflectionProbe::get_refreshMode", items[i]) == 1) ++every_frame;
    }
    log_line("probe: reflection probes: %u (realtime %u, every frame %u)", count, realtime, every_frame);
}

static void dump_renderers(void) {
    unsigned count = objects_of("UnityEngine", "Renderer", false);
    unsigned casting = 0, visible = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!alive(items[i])) continue;
        if (int_of("UnityEngine.Renderer::get_shadowCastingMode", items[i]) > 0) ++casting;
        if (bool_of("UnityEngine.Renderer::get_isVisible", items[i])) ++visible;
    }
    unsigned skinned = objects_of("UnityEngine", "SkinnedMeshRenderer", false);
    unsigned particles = objects_of("UnityEngine", "ParticleSystem", false);
    unsigned lod_groups = objects_of("UnityEngine", "LODGroup", false);
    unsigned terrains = objects_of("UnityEngine", "Terrain", false);
    log_line("probe: renderers: %u (visible %u, casting shadows %u); skinned %u; particle systems %u; LOD groups %u; terrains %u",
             count, visible, casting, skinned, particles, lod_groups, terrains);
}

void pg3d_probe_dump(Resolver resolver) {
    resolve = resolver;
    if (!domain_get || !domain_assemblies || !assembly_image || !class_from_name || !class_type || !type_object ||
        !class_field || !field_value || !object_class || !class_name || !class_namespace || !array_header_size ||
        !array_length || !string_chars || !string_length || !bind("UnityEngine.Object::FindObjectsOfType")) {
        pg3d_presentation_log("probe unavailable: missing IL2CPP exports or bindings.");
        return;
    }
    if (!cached_pointer) {
        void *object = find_class("UnityEngine", "Object");
        cached_pointer = object ? class_field(object, "m_CachedPtr") : NULL;
    }
    pg3d_presentation_log("probe: ---- scene inventory ----");
    dump_engine();
    dump_cameras();
    dump_volumes();
    dump_lights();
    dump_renderers();
    pg3d_presentation_log("probe: ---- end of inventory ----");
}
