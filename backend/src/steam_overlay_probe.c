// Read-only Steam overlay readiness: never initializes Steam, opens UI, or
// changes callbacks. Called from the unlocker's existing main-thread sampler.
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void pg3d_presentation_log(const char *message);

typedef void *(*SteamUtilsGetter)(void);
typedef bool (*SteamOverlayGetter)(void *utils);
typedef int (*SteamPipeGetter)(void);

static SteamUtilsGetter g_get_utils;
static SteamOverlayGetter g_is_overlay_enabled;
static SteamPipeGetter g_get_pipe;
static void *g_sdk_handle;

static bool has_basename(const char *path, const char *name) {
    if (path == NULL) return false;
    const char *base = strrchr(path, '/');
    return strcmp(base == NULL ? path : base + 1, name) == 0;
}

static bool resolve_api(void *handle) {
    SteamUtilsGetter utils = (SteamUtilsGetter)dlsym(handle, "SteamAPI_SteamUtils_v010");
    SteamOverlayGetter overlay =
        (SteamOverlayGetter)dlsym(handle, "SteamAPI_ISteamUtils_IsOverlayEnabled");
    SteamPipeGetter pipe = (SteamPipeGetter)dlsym(handle, "SteamAPI_GetHSteamPipe");
    if (utils == NULL || overlay == NULL || pipe == NULL) return false;
    g_get_utils = utils;
    g_is_overlay_enabled = overlay;
    g_get_pipe = pipe;
    return true;
}

void pg3d_steam_overlay_sample(void) {
    bool renderer_loaded = false;
    const char *sdk_path = NULL;
    const uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; ++i) {
        const char *name = _dyld_get_image_name(i);
        if (has_basename(name, "gameoverlayrenderer.dylib") ||
            has_basename(name, "gameoverlayrenderer32.dylib")) {
            renderer_loaded = true;
        }
        if (has_basename(name, "libsteam_api.dylib")) sdk_path = name;
    }

    if (g_get_utils == NULL && sdk_path != NULL) {
        // Unity can load its Steam API bundle with local symbol visibility.
        // RTLD_NOLOAD guarantees this observation cannot load another SDK.
        if (g_sdk_handle == NULL) {
            g_sdk_handle = dlopen(sdk_path, RTLD_LAZY | RTLD_NOLOAD);
        }
        if (g_sdk_handle == NULL || !resolve_api(g_sdk_handle)) {
            (void)resolve_api(RTLD_DEFAULT);
        }
        // Keep the one successful NOLOAD handle for process lifetime so the
        // cached pointers cannot outlive the loaded Steam API image.
    }

    const char *api_status = "not-loaded";
    const char *overlay_status = "unknown";
    if (sdk_path != NULL) api_status = "symbols-unavailable";
    if (g_get_utils != NULL) {
        // Do not request an interface until the game itself initialized Steam.
        if (g_get_pipe() == 0) {
            api_status = "not-initialized";
        } else {
            void *utils = g_get_utils();
            api_status = utils == NULL ? "interface-unavailable" : "ready";
            if (utils != NULL) {
                overlay_status = g_is_overlay_enabled(utils) ? "yes" : "no";
            }
        }
    }

    char message[256];
    snprintf(message, sizeof(message),
             "Steam overlay: renderer_loaded=%s; api=%s; IsOverlayEnabled=%s.",
             renderer_loaded ? "yes" : "no", api_status, overlay_status);
    pg3d_presentation_log(message);
}
