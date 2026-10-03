// Options file parsing. Values are validated per option here; anything
// unexpected falls back to the game's own value rather than a guess.
#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pg3d_options[OPT_COUNT] = {
    [0 ... OPT_COUNT - 1] = PG3D_GAME_VALUE,
};
OptionStatus pg3d_option_status[OPT_COUNT];
const char *const pg3d_option_keys[OPT_COUNT] = {
    [OPT_POSTFX] = "postfx",
    [OPT_AMBIENT_OCCLUSION] = "ambient-occlusion",
    [OPT_BLOOM] = "bloom",
    [OPT_COLOR_GRADING] = "color-grading",
    [OPT_AUTO_EXPOSURE] = "auto-exposure",
    [OPT_LENS_EFFECTS] = "lens-effects",
    [OPT_DOF_MOTION_BLUR] = "dof-motion-blur",
    [OPT_DEPTH_PASS] = "depth-pass",
    [OPT_FOG] = "fog",
    [OPT_SHADOWS] = "shadows",
    [OPT_SHADOW_QUALITY] = "shadow-quality",
    [OPT_PIXEL_LIGHTS] = "pixel-lights",
    [OPT_REFLECTIONS] = "reflections",
    [OPT_MSAA] = "msaa",
    [OPT_ANISO] = "aniso",
    [OPT_LOD] = "lod",
    [OPT_TEXTURES] = "textures",
    [OPT_SKIN_WEIGHTS] = "skin-weights",
    [OPT_SOFT_PARTICLES] = "soft-particles",
    [OPT_SOFT_VEGETATION] = "soft-vegetation",
    [OPT_PARTICLE_RAYCASTS] = "particle-raycasts",
    [OPT_ENGINE_THREADS] = "engine-threads",
    [OPT_GPU_PRIORITY] = "gpu-priority",
};

const char *pg3d_option_state_name(int state) {
    switch (state) {
    case OPT_STATE_APPLIED: return "applied";
    case OPT_STATE_ALREADY: return "already";
    case OPT_STATE_REJECTED: return "rejected";
    case OPT_STATE_UNAVAILABLE: return "unavailable";
    default: return "game";
    }
}

// The values each option accepts besides "game". Switches accept only 0 (off).
static bool valid(int option, long value) {
    switch (option) {
    case OPT_SHADOWS: return value == 0 || value == 1;
    case OPT_SHADOW_QUALITY: return value == 0 || value == 1;
    case OPT_PIXEL_LIGHTS: return value >= 0 && value <= 8;
    case OPT_MSAA: return value == 0 || value == 2 || value == 4;
    case OPT_ANISO: return value == 0 || value == 1;
    case OPT_LOD: return value >= 10 && value <= 1000;
    case OPT_TEXTURES: return value >= 1 && value <= 3;
    case OPT_SKIN_WEIGHTS: return value == 1 || value == 2 || value == 4;
    case OPT_PARTICLE_RAYCASTS: return value >= 4 && value <= 4096;
    case OPT_ENGINE_THREADS: return value == 1;
    case OPT_GPU_PRIORITY: return value == 1;
    default: return value == 0;
    }
}

static bool parse_long(const char *text, long *out) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!*text || !end || *end) return false;
    *out = value;
    return true;
}

bool pg3d_options_parse(const char *path, int values[OPT_COUNT], int *fps) {
    for (int i = 0; i < OPT_COUNT; ++i) values[i] = PG3D_GAME_VALUE;
    *fps = PG3D_FPS_UNSET;
    FILE *file = path ? fopen(path, "r") : NULL;
    if (!file) return false;
    char line[160];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        char *equals = strchr(line, '=');
        if (!equals) continue;
        *equals = 0;
        const char *key = line, *text = equals + 1;
        long value = 0;
        if (strcmp(key, "fps") == 0) {
            if (strcmp(text, "uncapped") == 0) *fps = -1;
            else if (parse_long(text, &value) && (value == -1 || value == 0 || (value >= 30 && value <= 1000)))
                *fps = value == 0 ? -1 : (int)value;
            continue;
        }
        for (int i = 0; i < OPT_COUNT; ++i) {
            if (strcmp(key, pg3d_option_keys[i]) != 0) continue;
            if (strcmp(text, "game") != 0 && parse_long(text, &value) && valid(i, value)) values[i] = (int)value;
            break;
        }
    }
    fclose(file);
    return true;
}

bool pg3d_options_any_managed(void) {
    for (int i = 0; i < OPT_COUNT; ++i)
        if (pg3d_options[i] != PG3D_GAME_VALUE) return true;
    return false;
}
