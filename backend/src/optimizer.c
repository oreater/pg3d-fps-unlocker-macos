// Session-only rendering budgets. Called on Unity's main queue after the
// inherited build-specific pacing wrappers have both passed validation.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef void *(*Resolver)(const char *);
extern void pg3d_effects_start(Resolver resolve);
extern void pg3d_effects_tick(int profile);
extern void pg3d_effects_frame(void);
extern void pg3d_presentation_log(const char *message);
typedef enum { INTEGER, FLOATING, BOOLEAN } Kind;
typedef struct {
    const char *name;
    Kind kind;
    double balanced, performance;
    void *get, *set;
    double baseline, applied;
    bool available, owned, failed;
} Setting;
static Setting settings[] = {
    {.name="shadowDistance", .kind=FLOATING, .balanced=25, .performance=15},
    {.name="shadowCascades", .kind=INTEGER, .balanced=2, .performance=1},
    {.name="shadowResolution", .kind=INTEGER, .balanced=1, .performance=0},
    {.name="shadows", .kind=INTEGER, .balanced=2, .performance=0},
    {.name="antiAliasing", .kind=INTEGER, .balanced=2, .performance=0},
    {.name="pixelLightCount", .kind=INTEGER, .balanced=2, .performance=1},
    {.name="lodBias", .kind=FLOATING, .balanced=1, .performance=0.6},
    {.name="anisotropicFiltering", .kind=INTEGER, .balanced=1, .performance=0},
    {.name="softParticles", .kind=BOOLEAN, .balanced=1, .performance=0},
    {.name="realtimeReflectionProbes", .kind=BOOLEAN, .balanced=1, .performance=0},
    // SkinWeights enum: 1, 2, 4 bones or 255 (unlimited); ordered by cost.
    {.name="skinWeights", .kind=INTEGER, .balanced=4, .performance=2},
    {.name="particleRaycastBudget", .kind=INTEGER, .balanced=256, .performance=64},
    {.name="softVegetation", .kind=BOOLEAN, .balanced=1, .performance=0},
};
static int active_profile = -1;
static bool initialized;
static bool in_frame;
static const char *control_path;
static void *(*camera_main)(void);
static bool (*camera_occlusion)(void *);
static float (*camera_far)(void *);
static int (*camera_width)(void *), (*camera_height)(void *);
static void *(*render_pipeline)(void);

static const char *profile_name(int profile) {
    return profile == 2 ? "performance" : profile == 1 ? "balanced" : "original";
}
static int parse_profile(const char *name) {
    if (name && strcmp(name, "original") == 0) return 0;
    if (name && strcmp(name, "balanced") == 0) return 1;
    if (name && strcmp(name, "performance") == 0) return 2;
    return -1;
}
static double read_setting(Setting *s) {
    if (s->kind == FLOATING) return ((float (*)(void))s->get)();
    if (s->kind == BOOLEAN) return ((bool (*)(void))s->get)();
    return ((int (*)(void))s->get)();
}
static void write_setting(Setting *s, double value) {
    if (s->kind == FLOATING) ((void (*)(float))s->set)((float)value);
    else if (s->kind == BOOLEAN) ((void (*)(bool))s->set)(value != 0);
    else ((void (*)(int))s->set)((int)value);
}
static bool equal_value(double a, double b) { return isfinite(a) && isfinite(b) && fabs(a-b) < 0.0001; }
static void setting_log(Setting *s, const char *action, double before, double after) {
    if (in_frame) return;
    char line[256];
    snprintf(line, sizeof(line), "optimizer %s: %s %.3g -> %.3g (Unity readback); baseline=%.3g.",
             action, s->name, before, after, s->baseline);
    pg3d_presentation_log(line);
}

static void apply_profile(int profile) {
    bool changed = profile != active_profile;
    for (size_t i=0; i<sizeof(settings)/sizeof(settings[0]); ++i) {
        Setting *s = &settings[i];
        if (!s->available || s->failed) continue;
        double current = read_setting(s);
        if (!isfinite(current) || current < 0) {
            s->failed = true;
            pg3d_presentation_log("optimizer: unexpected property value; stopped managing this property.");
            continue;
        }
        // Track observed game/user changes, including quality resets on scene
        // loads. Never raise an already-lower budget. Original releases control.
        if (!s->owned || !equal_value(current, s->applied)) s->baseline = current;
        double desired = profile == 0 ? s->baseline :
            fmin(s->baseline, profile == 1 ? s->balanced : s->performance);
        if (!equal_value(current, desired)) {
            write_setting(s, desired);
            double actual = read_setting(s);
            if (!equal_value(actual, desired)) {
                setting_log(s, "REJECTED", current, actual);
                // Restore the value immediately preceding our failed write.
                write_setting(s, current);
                s->failed = true;
                continue;
            }
            setting_log(s, profile == 0 ? "restored" : "applied", current, actual);
        }
        s->applied = desired;
        s->owned = profile != 0;
    }
    if (in_frame) pg3d_effects_frame();
    else pg3d_effects_tick(profile);
    active_profile = profile;
    if (changed) {
        char line[160];
        snprintf(line, sizeof(line), "optimizer profile active: %s; supported values checked against Unity.", profile_name(profile));
        pg3d_presentation_log(line);
    }
}

void pg3d_optimizer_frame(void) {
    if (!initialized || active_profile <= 0) return;
    in_frame = true;
    apply_profile(active_profile);
    in_frame = false;
}

void pg3d_optimizer_start(Resolver resolve, bool observe) {
    if (initialized || observe) return;
    const char *requested = getenv("PG3D_OPT_PROFILE");
    int profile = parse_profile(requested);
    if (profile < 0) {
        pg3d_presentation_log("optimizer disabled: invalid or missing explicit profile.");
        return;
    }
    control_path = getenv("PG3D_OPT_CONTROL");
    for (size_t i=0; i<sizeof(settings)/sizeof(settings[0]); ++i) {
        Setting *s = &settings[i];
        char binding[160];
        snprintf(binding, sizeof(binding), "UnityEngine.QualitySettings::get_%s", s->name);
        s->get = resolve(binding);
        snprintf(binding, sizeof(binding), "UnityEngine.QualitySettings::set_%s", s->name);
        s->set = resolve(binding);
        s->available = s->get != NULL && s->set != NULL;
        if (!s->available) {
            snprintf(binding, sizeof(binding), "optimizer unsupported: %s; skipped.", s->name);
            pg3d_presentation_log(binding);
        }
    }
    camera_main = (void *(*)(void))resolve("UnityEngine.Camera::get_main");
    camera_occlusion = (bool (*)(void *))resolve("UnityEngine.Camera::get_useOcclusionCulling");
    camera_far = (float (*)(void *))resolve("UnityEngine.Camera::get_farClipPlane");
    camera_width = (int (*)(void *))resolve("UnityEngine.Camera::get_pixelWidth");
    camera_height = (int (*)(void *))resolve("UnityEngine.Camera::get_pixelHeight");
    render_pipeline = (void *(*)(void))resolve("UnityEngine.QualitySettings::get_INTERNAL_renderPipeline");
    pg3d_effects_start(resolve);
    initialized = true;
    apply_profile(profile);
}

void pg3d_optimizer_tick(void) {
    if (!initialized) return;
    int profile = active_profile;
    if (control_path) {
        FILE *file = fopen(control_path, "r");
        if (file) {
            char value[64], extra;
            if (fscanf(file, "%63s %c", value, &extra) == 1) {
                int candidate = parse_profile(value);
                if (candidate >= 0) profile = candidate;
            }
            fclose(file);
        }
    }
    apply_profile(profile);
}

void pg3d_optimizer_sample(void) {
    if (!initialized) return;
    unsigned available = 0, failed = 0, matched = 0, reduced = 0;
    for (size_t i=0; i<sizeof(settings)/sizeof(settings[0]); ++i) {
        Setting *s = &settings[i];
        if (!s->available) continue;
        ++available;
        if (s->failed) { ++failed; continue; }
        double actual = read_setting(s);
        if (active_profile == 0 || equal_value(actual, s->applied)) ++matched;
        if (active_profile && actual < s->baseline - 0.0001) ++reduced;
    }
    char line[384];
    snprintf(line, sizeof(line), "optimizer status: profile=%s; verified=%u/%zu; reduced=%u; unavailable=%zu; failed=%u; custom_pipeline=%s. Readback is not proof of FPS gain.",
             profile_name(active_profile), matched, sizeof(settings)/sizeof(settings[0]), reduced,
             sizeof(settings)/sizeof(settings[0])-available, failed,
             render_pipeline ? (render_pipeline() ? "yes" : "no") : "unknown");
    pg3d_presentation_log(line);
    // Fresh camera lookup only. Never retain scene objects across a frame,
    // mutate culling masks, hide renderers, or change camera clipping distance.
    void *camera = camera_main ? camera_main() : NULL;
    if (camera && camera_occlusion && camera_far && camera_width && camera_height) {
        snprintf(line, sizeof(line), "optimizer camera audit: main=%dx%d; far_clip=%.1f; occlusion_flag=%s. Baked data and culled object counts unknown; clip distance unchanged.",
                 camera_width(camera), camera_height(camera), camera_far(camera), camera_occlusion(camera) ? "on" : "off");
        pg3d_presentation_log(line);
    } else pg3d_presentation_log("optimizer camera audit: no main camera or unavailable bindings.");
}
