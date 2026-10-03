// Session-only graphics options. Called on Unity's main queue after the
// inherited build-specific pacing wrappers have both passed validation.
// Every value is a ceiling (texture resolution: a floor) read back from Unity;
// a rejected value is restored immediately and left alone after that.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "options.h"

typedef void *(*Resolver)(const char *);
extern void pg3d_effects_start(Resolver resolve);
extern void pg3d_effects_tick(void);
extern void pg3d_effects_frame(void);
extern bool pg3d_effects_managing(void);
extern void pg3d_volumes_start(void);
extern void pg3d_volumes_apply(void);
extern void pg3d_volumes_frame(void);
extern bool pg3d_volumes_managing(void);
extern void pg3d_probe_dump(Resolver resolve);
extern void pg3d_set_requested_fps(int fps);
extern void pg3d_metal_tuning_apply(void);
extern void pg3d_threads_apply(void);
extern void pg3d_presentation_log(const char *message);
typedef enum { INTEGER, FLOATING, BOOLEAN } Kind;
typedef struct {
    const char *owner, *name;
    const char *renamed;  // newer Unity name, tried first (2022.2+)
    Kind kind;
    int option;
    bool floor;           // higher is cheaper (texture mip limit)
    void *get, *set;
    double game, applied;
    bool available, owned, failed;
    int state;
} Setting;
#define QS "UnityEngine.QualitySettings"
static Setting settings[] = {
    {.owner=QS, .name="shadows", .kind=INTEGER, .option=OPT_SHADOWS},
    {.owner=QS, .name="shadowDistance", .kind=FLOATING, .option=OPT_SHADOW_QUALITY},
    {.owner=QS, .name="shadowCascades", .kind=INTEGER, .option=OPT_SHADOW_QUALITY},
    {.owner=QS, .name="shadowResolution", .kind=INTEGER, .option=OPT_SHADOW_QUALITY},
    {.owner=QS, .name="antiAliasing", .kind=INTEGER, .option=OPT_MSAA},
    {.owner=QS, .name="anisotropicFiltering", .kind=INTEGER, .option=OPT_ANISO},
    {.owner=QS, .name="pixelLightCount", .kind=INTEGER, .option=OPT_PIXEL_LIGHTS},
    {.owner=QS, .name="lodBias", .kind=FLOATING, .option=OPT_LOD},
    {.owner=QS, .name="masterTextureLimit", .renamed="globalTextureMipmapLimit", .kind=INTEGER,
     .option=OPT_TEXTURES, .floor=true},
    {.owner=QS, .name="realtimeReflectionProbes", .kind=BOOLEAN, .option=OPT_REFLECTIONS},
    {.owner=QS, .name="softParticles", .kind=BOOLEAN, .option=OPT_SOFT_PARTICLES},
    {.owner=QS, .name="softVegetation", .kind=BOOLEAN, .option=OPT_SOFT_VEGETATION},
    // SkinWeights enum: 1, 2, 4 bones or 255 (unlimited); ordered by cost.
    {.owner=QS, .name="skinWeights", .kind=INTEGER, .option=OPT_SKIN_WEIGHTS},
    {.owner=QS, .name="particleRaycastBudget", .kind=INTEGER, .option=OPT_PARTICLE_RAYCASTS},
    {.owner="UnityEngine.RenderSettings", .name="fog", .kind=BOOLEAN, .option=OPT_FOG},
};
#undef QS
#define SETTING_COUNT (sizeof(settings) / sizeof(settings[0]))
static bool initialized;
static bool in_frame;
static bool managing;          // anything to enforce before each frame
static const char *control_path;
static char options_path[1024], state_path[1024], probe_path[1024];
static struct timespec options_stamp, probe_stamp;
static off_t options_size;
static unsigned options_seq;
static Resolver resolver;
static void *(*camera_main)(void);
static bool (*camera_occlusion)(void *);
static float (*camera_far)(void *);
static int (*camera_width)(void *), (*camera_height)(void *);
static void *(*render_pipeline)(void);

// The value an option asks for, in the setting's own units.
static double target(const Setting *s, int value) {
    if (s->option == OPT_SHADOW_QUALITY) {
        bool medium = value >= 1;
        if (strcmp(s->name, "shadowDistance") == 0) return medium ? 25 : 15;
        if (strcmp(s->name, "shadowCascades") == 0) return medium ? 2 : 1;
        return medium ? 1 : 0; // shadowResolution: Low = 0, Medium = 1
    }
    if (s->option == OPT_LOD) return value / 100.0;
    return value;
}
static double read_setting(const Setting *s) {
    if (s->kind == FLOATING) return ((float (*)(void))s->get)();
    if (s->kind == BOOLEAN) return ((bool (*)(void))s->get)();
    return ((int (*)(void))s->get)();
}
static void write_setting(const Setting *s, double value) {
    if (s->kind == FLOATING) ((void (*)(float))s->set)((float)value);
    else if (s->kind == BOOLEAN) ((void (*)(bool))s->set)(value != 0);
    else ((void (*)(int))s->set)((int)value);
}
static bool equal_value(double a, double b) { return isfinite(a) && isfinite(b) && fabs(a-b) < 0.0001; }
static void setting_log(const Setting *s, const char *action, double before, double after) {
    if (in_frame) return;
    char line[256];
    snprintf(line, sizeof(line), "optimizer %s: %s %.3g -> %.3g (Unity readback); game value %.3g.",
             action, s->name, before, after, s->game);
    pg3d_presentation_log(line);
}

static void apply_setting(Setting *s) {
    if (!s->available || s->failed) return;
    int value = pg3d_options[s->option];
    double current = read_setting(s);
    if (!isfinite(current) || current < 0) {
        s->failed = true;
        s->state = OPT_STATE_REJECTED;
        pg3d_presentation_log("optimizer: unexpected property value; stopped managing this property.");
        return;
    }
    // Anything that differs from what we last wrote is the game's own choice
    // (a quality reset on respawn or scene load): it becomes the new baseline.
    // Never raise an already-lower budget; "game" releases control.
    if (!s->owned || !equal_value(current, s->applied)) s->game = current;
    if (value == PG3D_GAME_VALUE) {
        if (s->owned && !equal_value(current, s->game)) {
            write_setting(s, s->game);
            setting_log(s, "restored", current, read_setting(s));
        }
        s->owned = false;
        s->state = OPT_STATE_GAME;
        return;
    }
    double wanted = s->floor ? fmax(s->game, target(s, value)) : fmin(s->game, target(s, value));
    if (!equal_value(current, wanted)) {
        write_setting(s, wanted);
        double actual = read_setting(s);
        if (!equal_value(actual, wanted)) {
            setting_log(s, "REJECTED", current, actual);
            // Restore the value immediately preceding our failed write.
            write_setting(s, current);
            s->failed = true;
            s->owned = false;
            s->state = OPT_STATE_REJECTED;
            return;
        }
        setting_log(s, "applied", current, actual);
    }
    s->applied = wanted;
    s->owned = true;
    s->state = equal_value(wanted, s->game) ? OPT_STATE_ALREADY : OPT_STATE_APPLIED;
}

// Several settings can serve one option: report the strongest state and the
// first setting's values (shadow quality reports shadow distance).
static void publish_settings(void) {
    static const int rank[] = {
        [OPT_STATE_GAME] = 0, [OPT_STATE_UNAVAILABLE] = 1, [OPT_STATE_ALREADY] = 2,
        [OPT_STATE_APPLIED] = 3, [OPT_STATE_REJECTED] = 4};
    bool seen[OPT_COUNT] = {false};
    for (size_t i = 0; i < SETTING_COUNT; ++i) {
        Setting *s = &settings[i];
        OptionStatus *o = &pg3d_option_status[s->option];
        int state = s->available ? s->state : OPT_STATE_UNAVAILABLE;
        if (!seen[s->option]) {
            seen[s->option] = true;
            o->state = state;
            o->game = s->game;
            o->current = s->available && !s->failed ? read_setting(s) : NAN;
        } else if (rank[state] > rank[o->state]) {
            o->state = state;
        }
    }
}

// "<control>.state": one line per option for the app. Written only from the
// timer (never per frame), replaced atomically.
static void write_state(void) {
    if (!state_path[0]) return;
    char temporary[1100];
    snprintf(temporary, sizeof(temporary), "%s.%d", state_path, getpid());
    FILE *file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "seq=%u\n", options_seq);
    for (int i = 0; i < OPT_COUNT; ++i) {
        OptionStatus *o = &pg3d_option_status[i];
        fprintf(file, "%s %s %.4g %.4g\n", pg3d_option_keys[i], pg3d_option_state_name(o->state),
                isfinite(o->game) ? o->game : -1, isfinite(o->current) ? o->current : -1);
    }
    bool ok = fclose(file) == 0;
    if (ok) rename(temporary, state_path);
    else unlink(temporary);
}

static void apply_all(void) {
    for (size_t i = 0; i < SETTING_COUNT; ++i) apply_setting(&settings[i]);
    publish_settings();
    // After the quality settings: effects add to their states (reflections).
    pg3d_effects_tick();
    pg3d_volumes_apply();
    // Engine and input settings keep their own state (no per-frame work here).
    pg3d_metal_tuning_apply();
    pg3d_threads_apply();
    bool quality_owned = false;
    for (size_t i = 0; i < SETTING_COUNT; ++i) quality_owned |= settings[i].owned;
    managing = quality_owned || pg3d_effects_managing() || pg3d_volumes_managing();
    write_state();
}

void pg3d_optimizer_frame(void) {
    if (!initialized || !managing) return;
    in_frame = true;
    for (size_t i = 0; i < SETTING_COUNT; ++i)
        if (settings[i].owned) apply_setting(&settings[i]);
    pg3d_effects_frame();
    pg3d_volumes_frame();
    in_frame = false;
}

static bool file_changed(const char *path, struct timespec *stamp, off_t *size) {
    struct stat info;
    if (!path[0] || stat(path, &info) != 0) return false;
    bool changed = info.st_mtimespec.tv_sec != stamp->tv_sec || info.st_mtimespec.tv_nsec != stamp->tv_nsec ||
                   (size && info.st_size != *size);
    *stamp = info.st_mtimespec;
    if (size) *size = info.st_size;
    return changed;
}

static void log_options(const char *prefix) {
    char line[1024];
    int used = snprintf(line, sizeof(line), "%s", prefix);
    unsigned managed = 0;
    for (int i = 0; i < OPT_COUNT && used > 0 && (size_t)used < sizeof(line) - 40; ++i) {
        if (pg3d_options[i] == PG3D_GAME_VALUE) continue;
        ++managed;
        used += snprintf(line + used, sizeof(line) - used, " %s=%d", pg3d_option_keys[i], pg3d_options[i]);
    }
    if (!managed && used > 0 && (size_t)used < sizeof(line) - 40) snprintf(line + used, sizeof(line) - used, " all game values");
    pg3d_presentation_log(line);
}

// Re-read the options file; returns true when the values changed.
static bool load_options(void) {
    int values[OPT_COUNT], fps = PG3D_FPS_UNSET;
    if (!pg3d_options_parse(options_path, values, &fps)) return false;
    if (fps != PG3D_FPS_UNSET) pg3d_set_requested_fps(fps);
    if (memcmp(values, pg3d_options, sizeof(values)) == 0) return false;
    memcpy(pg3d_options, values, sizeof(values));
    ++options_seq;
    return true;
}

// Every quarter second from the unlocker's timer: pick up a changed options
// file or a probe request without waiting for the five-second tick.
void pg3d_optimizer_poll(void) {
    if (!initialized) return;
    if (file_changed(options_path, &options_stamp, &options_size) && load_options()) {
        log_options("optimizer options changed:");
        apply_all();
    }
    if (file_changed(probe_path, &probe_stamp, NULL) && resolver) pg3d_probe_dump(resolver);
}

void pg3d_optimizer_start(Resolver resolve, bool observe) {
    if (initialized || observe) return;
    control_path = getenv("PG3D_OPT_CONTROL");
    if (!control_path || !*control_path) {
        pg3d_presentation_log("optimizer disabled: no options file for this session.");
        return;
    }
    snprintf(options_path, sizeof(options_path), "%s.options", control_path);
    snprintf(state_path, sizeof(state_path), "%s.state", control_path);
    snprintf(probe_path, sizeof(probe_path), "%s.probe", control_path);
    int values[OPT_COUNT], fps;
    if (!pg3d_options_parse(options_path, values, &fps)) {
        pg3d_presentation_log("optimizer disabled: options file missing.");
        return;
    }
    resolver = resolve;
    for (size_t i=0; i<SETTING_COUNT; ++i) {
        Setting *s = &settings[i];
        char binding[160];
        for (int attempt = s->renamed ? 0 : 1; attempt < 2; ++attempt) {
            const char *name = attempt == 0 ? s->renamed : s->name;
            snprintf(binding, sizeof(binding), "%s::get_%s", s->owner, name);
            s->get = resolve(binding);
            snprintf(binding, sizeof(binding), "%s::set_%s", s->owner, name);
            s->set = resolve(binding);
            if (s->get != NULL && s->set != NULL) {
                s->name = name;
                break;
            }
        }
        s->available = s->get != NULL && s->set != NULL;
        if (s->available) s->game = read_setting(s);
        else {
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
    pg3d_volumes_start();
    initialized = true;
    file_changed(options_path, &options_stamp, &options_size);
    file_changed(probe_path, &probe_stamp, NULL);
    load_options();
    apply_all();
    log_options("optimizer active; options:");
}

void pg3d_optimizer_tick(void) {
    if (!initialized) return;
    apply_all();
}

void pg3d_optimizer_sample(void) {
    if (!initialized) return;
    unsigned counts[OPT_STATE_UNAVAILABLE + 1] = {0};
    for (int i = 0; i < OPT_COUNT; ++i) ++counts[pg3d_option_status[i].state];
    char line[384];
    snprintf(line, sizeof(line), "optimizer status: options=%d; applied=%u; already=%u; game=%u; rejected=%u; unavailable=%u; custom_pipeline=%s. Readback is not proof of FPS gain.",
             OPT_COUNT, counts[OPT_STATE_APPLIED], counts[OPT_STATE_ALREADY], counts[OPT_STATE_GAME],
             counts[OPT_STATE_REJECTED], counts[OPT_STATE_UNAVAILABLE],
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
