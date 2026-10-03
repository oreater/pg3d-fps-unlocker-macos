#include <assert.h>
#include <stdarg.h>
#include "../src/options.c"
#include "../src/optimizer.c"
static bool effects_on, volumes_on;
static int fps_requested = PG3D_FPS_UNSET, probes;
void pg3d_effects_start(Resolver resolve) { (void)resolve; }
void pg3d_effects_tick(void) {}
void pg3d_effects_frame(void) {}
bool pg3d_effects_managing(void) { return effects_on; }
void pg3d_volumes_start(void) {}
void pg3d_volumes_apply(void) {}
void pg3d_volumes_frame(void) {}
bool pg3d_volumes_managing(void) { return volumes_on; }
void pg3d_probe_dump(Resolver resolve) { (void)resolve; ++probes; }
void pg3d_set_requested_fps(int fps) { fps_requested = fps; }
void pg3d_presentation_log(const char *message) { (void)message; }
static int engine_applies;
void pg3d_metal_tuning_apply(void) { ++engine_applies; }
void pg3d_threads_apply(void) { ++engine_applies; }
static int lights=8, aa=8, texture_limit=0;
static float lod=2;
static bool fog=true;
static int get_lights(void) { return lights; }
static void set_lights(int n) { lights=n; }
static int get_aa(void) { return aa; }
static void reject_aa(int n) { (void)n; }
static float get_lod(void) { return lod; }
static void set_lod(float n) { lod=n; }
static int get_limit(void) { return texture_limit; }
static void set_limit(int n) { texture_limit=n; }
static bool unity2022;
static bool get_fog(void) { return fog; }
static void set_fog(bool n) { fog=n; }
static void *resolve(const char *name) {
    if (strcmp(name, "UnityEngine.QualitySettings::get_pixelLightCount")==0) return get_lights;
    if (strcmp(name, "UnityEngine.QualitySettings::set_pixelLightCount")==0) return set_lights;
    if (strcmp(name, "UnityEngine.QualitySettings::get_lodBias")==0) return get_lod;
    if (strcmp(name, "UnityEngine.QualitySettings::set_lodBias")==0) return set_lod;
    if (strcmp(name, "UnityEngine.QualitySettings::get_antiAliasing")==0) return get_aa;
    if (strcmp(name, "UnityEngine.QualitySettings::set_antiAliasing")==0) return reject_aa;
    if (strcmp(name, "UnityEngine.QualitySettings::get_masterTextureLimit")==0) return unity2022 ? NULL : get_limit;
    if (strcmp(name, "UnityEngine.QualitySettings::set_masterTextureLimit")==0) return unity2022 ? NULL : set_limit;
    if (strcmp(name, "UnityEngine.QualitySettings::get_globalTextureMipmapLimit")==0) return unity2022 ? get_limit : NULL;
    if (strcmp(name, "UnityEngine.QualitySettings::set_globalTextureMipmapLimit")==0) return unity2022 ? set_limit : NULL;
    if (strcmp(name, "UnityEngine.RenderSettings::get_fog")==0) return get_fog;
    if (strcmp(name, "UnityEngine.RenderSettings::set_fog")==0) return set_fog;
    return NULL;
}
static char control[512], options_file[600], state_file[600], probe_file[600];
static void write_options(const char *text) {
    static int generation;
    // Distinct content length each time, so the change is seen within one second.
    FILE *f = fopen(options_file, "w"); assert(f);
    fprintf(f, "%s\n# %*d\n", text, ++generation, 0);
    fclose(f);
}
static bool state_has(const char *line) {
    FILE *f = fopen(state_file, "r"); assert(f);
    char buffer[4096]; size_t n = fread(buffer, 1, sizeof(buffer) - 1, f); buffer[n] = 0; fclose(f);
    return strstr(buffer, line) != NULL;
}
int main(void) {
    char directory[] = "/tmp/optimizer-test.XXXXXX";
    assert(mkdtemp(directory));
    snprintf(control, sizeof(control), "%s/run.log.profile", directory);
    snprintf(options_file, sizeof(options_file), "%s.options", control);
    snprintf(state_file, sizeof(state_file), "%s.state", control);
    snprintf(probe_file, sizeof(probe_file), "%s.probe", control);
    setenv("PG3D_OPT_CONTROL", control, 1);

    pg3d_optimizer_start(resolve, true);
    assert(!initialized && lights==8); // Observation cannot mutate settings.
    pg3d_optimizer_start(resolve, false);
    assert(!initialized); // No options file: nothing is managed.

    write_options("pixel-lights=2\nlod=100\nmsaa=2\nfps=240");
    pg3d_optimizer_start(resolve, false);
    assert(initialized && lights==2 && lod==1 && aa==8 && fps_requested==240);
    assert(pg3d_option_status[OPT_MSAA].state==OPT_STATE_REJECTED); // Rejected setter is reported, not verified.
    assert(pg3d_option_status[OPT_PIXEL_LIGHTS].state==OPT_STATE_APPLIED);
    assert(pg3d_option_status[OPT_PIXEL_LIGHTS].game==8 && pg3d_option_status[OPT_PIXEL_LIGHTS].current==2);
    assert(pg3d_option_status[OPT_SHADOWS].state==OPT_STATE_UNAVAILABLE); // Missing binding.
    assert(state_has("pixel-lights applied 8 2\n") && state_has("msaa rejected"));
    assert(managing);
    assert(engine_applies > 0); // Engine settings are brought in line with every apply.

    write_options("pixel-lights=1\nlod=60\nfps=uncapped");
    pg3d_optimizer_poll(); // Live change without waiting for the five-second tick.
    assert(lights==1 && fabs(lod-0.6)<0.0001 && fps_requested==-1);
    lights=7; pg3d_optimizer_frame(); assert(lights==1); // Enforce a spawn reset without waiting for telemetry.
    lights=8; pg3d_optimizer_frame(); // Last game-requested value is restored below.
    write_options("pixel-lights=game\nlod=game");
    pg3d_optimizer_poll();
    assert(lights==8 && lod==2); // "game" restores the game's own values.
    assert(pg3d_option_status[OPT_PIXEL_LIGHTS].state==OPT_STATE_GAME && !managing);
    lights=0; lod=0.4;
    write_options("pixel-lights=2\nlod=100");
    pg3d_optimizer_poll();
    assert(lights==0 && fabs(lod-0.4)<0.0001); // Never increase existing cost.
    assert(pg3d_option_status[OPT_PIXEL_LIGHTS].state==OPT_STATE_ALREADY);
    lights=6; lod=3; // Simulate game resetting quality on map load.
    pg3d_optimizer_tick();
    assert(lights==2 && fabs(lod-1)<0.0001);
    write_options("textures=1\nfog=0");
    pg3d_optimizer_poll();
    assert(lights==6 && lod==3); // Restore most recent external values.
    assert(texture_limit==1 && !fog); // Texture limit is a floor; fog is a render setting.
    texture_limit=2; pg3d_optimizer_frame(); assert(texture_limit==2); // A lower resolution the game chose is kept.
    fog=true; pg3d_optimizer_frame(); assert(!fog); // Scene load turned fog back on.
    write_options("");
    pg3d_optimizer_poll();
    assert(texture_limit==2 && fog); // The game's latest values.
    pg3d_optimizer_poll(); // Unchanged file: nothing reloaded.

    int values[OPT_COUNT], fps;
    FILE *f = fopen(options_file, "w"); assert(f);
    fputs("pixel-lights=99\nmsaa=3\nshadows=2\nlod=5\nbloom=1\nskin-weights=3\nunknown=0\nfps=10\npostfx=0\n"
          "engine-threads=2\ngpu-priority=0\n", f); fclose(f);
    assert(pg3d_options_parse(options_file, values, &fps));
    for (int i = 0; i < OPT_COUNT; ++i) assert(values[i] == (i == OPT_POSTFX ? 0 : PG3D_GAME_VALUE)); // Invalid values fall back to the game's.
    f = fopen(options_file, "w"); assert(f); fputs("engine-threads=1\ngpu-priority=1\n", f); fclose(f);
    assert(pg3d_options_parse(options_file, values, &fps));
    assert(values[OPT_ENGINE_THREADS] == 1 && values[OPT_GPU_PRIORITY] == 1);
    assert(fps == PG3D_FPS_UNSET);
    assert(!pg3d_options_parse("/nonexistent/options", values, &fps));

    f = fopen(probe_file, "w"); assert(f); fputs("1\n", f); fclose(f);
    pg3d_optimizer_poll(); assert(probes == 1); // Probe requests are picked up by the poll.
    pg3d_optimizer_sample(); // Missing camera/pipeline bindings remain optional.
    // Unity 2022 renamed the texture limit; the newer name is used when registered.
    const Setting *texture = NULL;
    for (size_t i = 0; i < SETTING_COUNT; ++i) if (settings[i].option == OPT_TEXTURES) texture = &settings[i];
    assert(texture && strcmp(texture->name, "masterTextureLimit") == 0 && texture->available);
    initialized = false; unity2022 = true;
    for (size_t i = 0; i < SETTING_COUNT; ++i) if (settings[i].option == OPT_TEXTURES) settings[i].name = "masterTextureLimit";
    pg3d_optimizer_start(resolve, false);
    assert(initialized && strcmp(texture->name, "globalTextureMipmapLimit") == 0 && texture->available);
    unlink(options_file); unlink(state_file); unlink(probe_file); unlink(control); rmdir(directory);
    puts("PASS: observe, missing options, rejected setters, clamping, live option changes, game resets, restore, texture floor, fog, validation, state file, probe requests, engine option parsing, Unity 2022 texture limit name.");
}
