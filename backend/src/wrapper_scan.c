// Finds a Unity IL2CPP icall wrapper by its binding name, for game builds whose
// wrapper addresses are not listed in pg3d_fps_unlock.c yet. Reads the loaded
// GameAssembly image only: the binding-name string in a C-string section, then
// the one wrapper whose lazy cache lookup loads that exact string.
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// mov rax,[rip+cache]; test rax,rax; je +2; jmp rax; push rbp; mov rbp,rsp;
// push rbx; push rax; lea rax,[rip+name]. Bytes 3..6 (cache) and 23..26 (name)
// are displacements.
const uint8_t pg3d_wrapper_prefix[23] = {
    0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x85, 0xC0, 0x74, 0x02, 0xFF, 0xE0,
    0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x8D, 0x05,
};

typedef bool (*SectionVisitor)(const uint8_t *start, size_t size, void *context);

static void each_section(const struct mach_header_64 *header, uintptr_t slide, bool code,
                         SectionVisitor visit, void *context) {
    const uint8_t *command_bytes = (const uint8_t *)(header + 1);
    for (uint32_t c = 0; c < header->ncmds; ++c) {
        const struct load_command *command = (const struct load_command *)command_bytes;
        if (command->cmdsize < sizeof(*command)) return;
        if (command->cmd == LC_SEGMENT_64 && command->cmdsize >= sizeof(struct segment_command_64)) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            const struct section_64 *sections = (const struct section_64 *)(segment + 1);
            uint32_t count = segment->nsects;
            if ((command->cmdsize - sizeof(*segment)) / sizeof(*sections) < count) return;
            for (uint32_t s = 0; s < count && strncmp(segment->segname, SEG_TEXT, 16) == 0; ++s) {
                const struct section_64 *section = &sections[s];
                bool wanted = code ? (section->flags & S_ATTR_PURE_INSTRUCTIONS) != 0
                                   : (section->flags & SECTION_TYPE) == S_CSTRING_LITERALS;
                if (wanted && section->size != 0 &&
                    visit((const uint8_t *)(slide + section->addr), (size_t)section->size, context))
                    return;
            }
        }
        command_bytes += command->cmdsize;
    }
}

typedef struct {
    const char *name;
    size_t length;          // including the terminating NUL
    const uint8_t *string;  // whole C string in the image
    const uint8_t *wrapper;
    unsigned matches;
} Search;

static bool find_string(const uint8_t *start, size_t size, void *context) {
    Search *search = context;
    const uint8_t *end = start + size, *at = start;
    while ((at = memmem(at, (size_t)(end - at), search->name, search->length)) != NULL) {
        if (at == start || at[-1] == 0) {  // not the tail of a longer string
            search->string = at;
            return true;
        }
        ++at;
    }
    return false;
}

static bool find_wrapper(const uint8_t *start, size_t size, void *context) {
    Search *search = context;
    const uint8_t *end = start + size, *at = start + 7;
    const size_t tail = sizeof(pg3d_wrapper_prefix) - 7;
    while (at + tail + 4 <= end &&
           (at = memmem(at, (size_t)(end - at), pg3d_wrapper_prefix + 7, tail)) != NULL) {
        const uint8_t *wrapper = at - 7;
        if (at + tail + 4 > end) break;
        if (memcmp(wrapper, pg3d_wrapper_prefix, 3) == 0) {
            int32_t displacement;
            memcpy(&displacement, wrapper + 23, sizeof(displacement));
            if ((uintptr_t)(wrapper + 27) + (intptr_t)displacement == (uintptr_t)search->string) {
                search->wrapper = wrapper;
                ++search->matches;
            }
        }
        ++at;
    }
    return false;  // keep looking: more than one match means ambiguous
}

// Returns the wrapper's address, or NULL when the string or the wrapper is
// missing or more than one wrapper loads the same string.
const uint8_t *pg3d_find_icall_wrapper(const struct mach_header_64 *header, uintptr_t slide,
                                       const char *binding_name) {
    Search search = {.name = binding_name, .length = strlen(binding_name) + 1};
    each_section(header, slide, false, find_string, &search);
    if (search.string == NULL) return NULL;
    each_section(header, slide, true, find_wrapper, &search);
    return search.matches == 1 ? search.wrapper : NULL;
}
