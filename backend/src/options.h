// Live graphics options shared by the injected modules. The launcher and the
// app write "<control>.options" (key=value lines; "game" keeps the game's own
// value) and the library reloads it within a quarter second of a change.
// Labels, choices and presets live in backend/options.tsv; keys must match.
// The engine and input keys at the end of the list are regular options in the
// file, but their preset columns are "-": presets leave them as they are.
#pragma once
#include <stdbool.h>

#define PG3D_GAME_VALUE (-1)
#define PG3D_FPS_UNSET (-2)

enum {
    OPT_POSTFX,            // 0 = camera post-processing stack off (plus its depth/HDR passes)
    OPT_AMBIENT_OCCLUSION, // 0 = post-processing AO and legacy SSAO off
    OPT_BLOOM,
    OPT_COLOR_GRADING,
    OPT_AUTO_EXPOSURE,
    OPT_LENS_EFFECTS,      // chromatic aberration, vignette, grain, lens distortion
    OPT_DOF_MOTION_BLUR,
    OPT_DEPTH_PASS,        // 0 = no camera depth texture on screen cameras
    OPT_FOG,               // 0 = RenderSettings.fog off
    OPT_SHADOWS,           // 1 = hard only, 0 = off
    OPT_SHADOW_QUALITY,    // 1 = medium (25 m, 2 cascades), 0 = low (15 m, 1 cascade)
    OPT_PIXEL_LIGHTS,
    OPT_REFLECTIONS,       // 0 = real-time reflection probes and screen-space reflections off
    OPT_MSAA,
    OPT_ANISO,             // 1 = per texture, 0 = off
    OPT_LOD,               // LOD bias x100
    OPT_TEXTURES,          // mip levels dropped: 1 = half, 2 = quarter resolution
    OPT_SKIN_WEIGHTS,      // bones per vertex
    OPT_SOFT_PARTICLES,
    OPT_SOFT_VEGETATION,
    OPT_PARTICLE_RAYCASTS,
    // Engine and input settings (not ceilings, not part of the graphics presets).
    OPT_ENGINE_THREADS,    // 1 = Unity job workers and render thread at high priority
    OPT_GPU_PRIORITY,      // 1 = the game's Metal command queues at high GPU priority
    OPT_COUNT
};

enum {
    OPT_STATE_GAME,        // not managed: the game's value is in use
    OPT_STATE_APPLIED,     // our value is in effect (Unity readback)
    OPT_STATE_ALREADY,     // the game already runs this low or lower
    OPT_STATE_REJECTED,    // Unity refused the value; restored and left alone
    OPT_STATE_UNAVAILABLE, // not present in this game build or scene
};

typedef struct {
    int state;
    double game;           // the game's own value (effects: how many exist)
    double current;        // Unity readback now (effects: how many are on)
} OptionStatus;

extern int pg3d_options[OPT_COUNT];
extern OptionStatus pg3d_option_status[OPT_COUNT];
extern const char *const pg3d_option_keys[OPT_COUNT];

// Parse an options file into values (every key defaults to the game value).
// Returns false when the file cannot be read. *fps receives the "fps" key, or
// PG3D_FPS_UNSET when absent or invalid.
bool pg3d_options_parse(const char *path, int values[OPT_COUNT], int *fps);
// True when any option asks for something other than the game's value.
bool pg3d_options_any_managed(void);
const char *pg3d_option_state_name(int state);
