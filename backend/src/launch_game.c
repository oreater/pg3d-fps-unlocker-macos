// Detach the game into its own session before exec, preserving Steam/DYLD env.
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Match Steam's own preloads while retaining additional user-provided preloads.
// The installed Valve libraries are referenced in place, never copied/modified.
static int append_preloads(char **result, const char *list) {
    if (list == NULL || *list == '\0') return 0;
    char *copy = strdup(list);
    if (copy == NULL) return -1;
    char *state = NULL;
    for (char *entry = strtok_r(copy, ":", &state); entry != NULL;
         entry = strtok_r(NULL, ":", &state)) {
        size_t length = strlen(entry);
        const char *cursor = *result;
        int found = 0;
        while (*cursor) {
            size_t current_length = strcspn(cursor, ":");
            if (current_length == length && strncmp(cursor, entry, length) == 0) {
                found = 1;
                break;
            }
            cursor += current_length;
            if (*cursor == ':') ++cursor;
        }
        if (found) continue;
        char *expanded = NULL;
        if (asprintf(&expanded, "%s:%s", *result, entry) < 0) {
            free(copy);
            return -1;
        }
        free(*result);
        *result = expanded;
    }
    free(copy);
    return 0;
}

static char *make_preloads(const char *steam_dir, const char *unlocker) {
    char *result = NULL;
    if (asprintf(&result, "%s/steamloader.dylib:%s/gameoverlayrenderer.dylib:%s",
                 steam_dir, steam_dir, unlocker) < 0) {
        return NULL;
    }
    if (append_preloads(&result, getenv("DYLD_INSERT_LIBRARIES")) != 0 ||
        append_preloads(&result, getenv("STEAM_DYLD_INSERT_LIBRARIES")) != 0) {
        free(result);
        return NULL;
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr, "Expected executable, dylib, FPS, log path, observe flag, Steam binary directory[, game arguments...].\n");
        return 2;
    }
    int handshake[2];
    if (pipe(handshake) != 0) { perror("pipe"); return 1; }
    if (fcntl(handshake[1], F_SETFD, FD_CLOEXEC) < 0) { perror("fcntl"); return 1; }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        close(handshake[0]);
        int error = 0;
        if (setsid() < 0) { error = errno; goto fail; }
        signal(SIGHUP, SIG_IGN);
        int input = open("/dev/null", O_RDONLY);
        int output = open(argv[4], O_WRONLY | O_APPEND | O_CREAT, 0600);
        if (input < 0 || output < 0) { error = errno; goto fail; }
        if (dup2(input, STDIN_FILENO) < 0 || dup2(output, STDOUT_FILENO) < 0 ||
            dup2(output, STDERR_FILENO) < 0) { error = errno; goto fail; }
        close(input);
        close(output);
        char *preloads = make_preloads(argv[6], argv[2]);
        if (preloads == NULL) { error = ENOMEM; goto fail; }
        if (setenv("DYLD_INSERT_LIBRARIES", preloads, 1) ||
            setenv("STEAM_DYLD_INSERT_LIBRARIES", preloads, 1) ||
            setenv("PG3D_UNLOCK_FPS", argv[3], 1) ||
            setenv("PG3D_UNLOCK_LOG", argv[4], 1) ||
            setenv("PG3D_UNLOCK_OBSERVE", argv[5], 1) ||
            setenv("SteamClientLaunch", "1", 1) ||
            setenv("SteamEnv", "1", 1) ||
            setenv("SteamOverlayGameId", "2524890", 1) ||
            setenv("SteamAppId", "2524890", 1) ||
            setenv("SteamGameId", "2524890", 1)) { error = errno; goto fail; }
        free(preloads);
        dprintf(STDERR_FILENO, "[Testificate] Steam loader and overlay requested from: %s\n", argv[6]);
        // Unity reads this Boolean during startup, before IL2CPP is ready.
        // Its default display-link blit path capped Metal acquisition at 60 Hz
        // on the tested ProMotion Mac even with an uncapped engine frame loop.
        int observe = strcmp(argv[5], "1") == 0;
        if (observe ? unsetenv("UNITY_DISPLAYLINK_BLIT") :
                      setenv("UNITY_DISPLAYLINK_BLIT", "0", 1)) {
            error = errno;
            goto fail;
        }
        dprintf(STDERR_FILENO, "[Launcher] UNITY_DISPLAYLINK_BLIT=%s; %s.\n",
                observe ? "unset" : "0",
                observe ? "original display-link behavior" : "60 Hz display-link blit path disabled");
        // Optional engine arguments (for example -force-gfx-jobs native) follow
        // the fixed parameters and are passed to the game unchanged.
        char **game_argv = calloc((size_t)argc - 5, sizeof(char *));
        if (game_argv == NULL) { error = ENOMEM; goto fail; }
        game_argv[0] = argv[1];
        for (int i = 7; i < argc; ++i) {
            game_argv[i - 6] = argv[i];
            dprintf(STDERR_FILENO, "[Launcher] Game argument: %s\n", argv[i]);
        }
        execv(argv[1], game_argv);
        error = errno;
    fail:
        (void)write(handshake[1], &error, sizeof(error));
        _exit(1);
    }
    close(handshake[1]);
    int error = 0;
    ssize_t count;
    do { count = read(handshake[0], &error, sizeof(error)); } while (count < 0 && errno == EINTR);
    close(handshake[0]);
    if (count != 0) {
        if (count > 0) errno = error;
        perror("launch game");
        (void)waitpid(child, NULL, 0);
        return 1;
    }
    printf("%d\n", child);
    return 0;
}
