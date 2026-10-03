#include <assert.h>
#include <stdio.h>
#include "../src/wrapper_scan.c"

// A fake GameAssembly image: one __TEXT segment with a code section ("il2cpp",
// like Unity's) and a C-string section, laid out at vmaddr == file offset.
enum { kCode = 0x400, kStrings = 0x2000, kSize = 0x3000 };
static uint8_t image[kSize];
static const char *target = "UnityEngine.Application::set_targetFrameRate(System.Int32)";

static void put_wrapper(size_t at, size_t string_at) {
    memcpy(image + at, pg3d_wrapper_prefix, sizeof(pg3d_wrapper_prefix));
    int32_t cache = 0x1000, name = (int32_t)string_at - (int32_t)(at + 27);
    memcpy(image + at + 3, &cache, 4);
    memcpy(image + at + 23, &name, 4);
}

int main(void) {
    struct mach_header_64 *header = (struct mach_header_64 *)image;
    header->magic = MH_MAGIC_64;
    header->ncmds = 1;
    struct segment_command_64 *segment = (struct segment_command_64 *)(header + 1);
    segment->cmd = LC_SEGMENT_64;
    segment->nsects = 2;
    segment->cmdsize = sizeof(*segment) + 2 * sizeof(struct section_64);
    strcpy(segment->segname, SEG_TEXT);
    struct section_64 *sections = (struct section_64 *)(segment + 1);
    strcpy(sections[0].sectname, "il2cpp");
    sections[0].addr = kCode; sections[0].size = kStrings - kCode;
    sections[0].flags = S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
    strcpy(sections[1].sectname, "__cstring");
    sections[1].addr = kStrings; sections[1].size = kSize - kStrings;
    sections[1].flags = S_CSTRING_LITERALS;
    const uintptr_t slide = (uintptr_t)image;

    // A longer string ending in the same text comes first; it must not match.
    const size_t decoy = kStrings + 1, real = decoy + 1 + strlen(target) + 1 + 1;
    image[kStrings] = 0;
    memcpy(image + decoy, "x", 1);
    memcpy(image + decoy + 1, target, strlen(target) + 1);
    memcpy(image + real, target, strlen(target) + 1);

    assert(pg3d_find_icall_wrapper(header, slide, target) == NULL);  // no wrapper yet
    put_wrapper(kCode + 0x10, decoy + 1);                            // loads the decoy's tail
    assert(pg3d_find_icall_wrapper(header, slide, target) == NULL);
    put_wrapper(kCode + 0x100, real);
    assert(pg3d_find_icall_wrapper(header, slide, target) == image + kCode + 0x100);
    assert(pg3d_find_icall_wrapper(header, slide, "UnityEngine.Application::set_targetFrameRate") == NULL);
    // A different first instruction is not a wrapper.
    image[kCode + 0x100] = 0x90;
    assert(pg3d_find_icall_wrapper(header, slide, target) == NULL);
    image[kCode + 0x100] = 0x48;
    // Two wrappers for one binding are ambiguous: refuse.
    put_wrapper(kCode + 0x200, real);
    assert(pg3d_find_icall_wrapper(header, slide, target) == NULL);
    // Code outside instruction sections is ignored.
    memset(image + kCode + 0x200, 0, 32);
    sections[0].flags = 0;
    assert(pg3d_find_icall_wrapper(header, slide, target) == NULL);
    puts("PASS: icall wrapper scan finds the one wrapper by exact binding name; ignores suffix strings, non-wrappers, ambiguity, data sections.");
    return 0;
}
