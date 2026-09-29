// Exercise the environment builder only; never launches or loads game/Steam.
#define main launch_game_main
#include "../src/launch_game.c"
#undef main
#include <assert.h>

static void check(const char *expected) {
    char *actual = make_preloads("/Steam With Spaces", "/Tool/Unlock.dylib");
    assert(actual != NULL);
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "Unexpected preload list: %s\n", actual);
        abort();
    }
    free(actual);
}

int main(void) {
    const char *base = "/Steam With Spaces/steamloader.dylib:/Steam With Spaces/gameoverlayrenderer.dylib:/Tool/Unlock.dylib";
    unsetenv("DYLD_INSERT_LIBRARIES");
    unsetenv("STEAM_DYLD_INSERT_LIBRARIES");
    check(base);
    setenv("DYLD_INSERT_LIBRARIES", base, 1);
    setenv("STEAM_DYLD_INSERT_LIBRARIES", base, 1);
    check(base);
    setenv("DYLD_INSERT_LIBRARIES", ":/Extra One.dylib::/Extra One.dylib:", 1);
    setenv("STEAM_DYLD_INSERT_LIBRARIES", "/Extra Two.dylib:/Extra One.dylib:/Tool/Unlock.dylib", 1);
    check("/Steam With Spaces/steamloader.dylib:/Steam With Spaces/gameoverlayrenderer.dylib:/Tool/Unlock.dylib:/Extra One.dylib:/Extra Two.dylib");
    setenv("DYLD_INSERT_LIBRARIES", "", 1);
    setenv("STEAM_DYLD_INSERT_LIBRARIES", "", 1);
    check(base);
    puts("PASS: default preloads, spaces, both inherited lists, deduplication, empty entries.");
    return 0;
}
