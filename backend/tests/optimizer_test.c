#include <assert.h>
#include <stdarg.h>
#include "../src/optimizer.c"
void pg3d_effects_start(Resolver resolve) { (void)resolve; }
void pg3d_effects_tick(int profile) { (void)profile; }
void pg3d_effects_frame(void) {}
void pg3d_presentation_log(const char *message) { (void)message; }
static int lights=8, aa=8;
static float lod=2;
static int get_lights(void) { return lights; }
static void set_lights(int n) { lights=n; }
static int get_aa(void) { return aa; }
static void reject_aa(int n) { (void)n; }
static float get_lod(void) { return lod; }
static void set_lod(float n) { lod=n; }
static void *resolve(const char *name) {
    if (strcmp(name, "UnityEngine.QualitySettings::get_pixelLightCount")==0) return get_lights;
    if (strcmp(name, "UnityEngine.QualitySettings::set_pixelLightCount")==0) return set_lights;
    if (strcmp(name, "UnityEngine.QualitySettings::get_lodBias")==0) return get_lod;
    if (strcmp(name, "UnityEngine.QualitySettings::set_lodBias")==0) return set_lod;
    if (strcmp(name, "UnityEngine.QualitySettings::get_antiAliasing")==0) return get_aa;
    if (strcmp(name, "UnityEngine.QualitySettings::set_antiAliasing")==0) return reject_aa;
    return NULL;
}
int main(void) {
    setenv("PG3D_OPT_PROFILE", "balanced", 1);
    pg3d_optimizer_start(resolve, true);
    assert(!initialized && lights==8); // Observation cannot mutate settings.
    pg3d_optimizer_start(resolve, false);
    assert(initialized && lights==2 && lod==1 && aa==8);
    assert(settings[4].failed); // Rejected setter is not reported as verified.
    apply_profile(2);
    assert(lights==1 && fabs(lod-0.6)<0.0001);
    lights=7; pg3d_optimizer_frame(); assert(lights==1); // Enforce a spawn reset without waiting for telemetry.
    lights=8; pg3d_optimizer_frame(); // Last game-requested value is restored below.
    apply_profile(0);
    assert(lights==8 && lod==2); // Switching profiles restores the original.
    lights=0; lod=0.4;
    apply_profile(1);
    assert(lights==0 && fabs(lod-0.4)<0.0001); // Never increase existing cost.
    lights=6; lod=3; // Simulate game resetting quality on map load.
    apply_profile(2);
    assert(lights==1 && fabs(lod-0.6)<0.0001);
    apply_profile(0);
    assert(lights==6 && lod==3); // Restore most recent external values.
    lights=4; pg3d_optimizer_tick(); assert(lights==4); // Original releases ownership.
    assert(parse_profile("bogus")==-1);
    assert(parse_profile(NULL)==-1);
    pg3d_optimizer_sample(); // Missing camera/pipeline bindings remain optional.
    puts("PASS: observe, missing bindings, rejected setters, clamping, profile switching, game quality reset, restore.");
}
