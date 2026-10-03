// GPU priority against this Mac's real Metal device. Built
// x86_64 like the game, so it runs under Rosetta the same way.
#include <assert.h>
#include "../src/options.c"
#include "../src/metal_tuning.m"
void pg3d_presentation_log(const char *message) { fprintf(stderr, "  log: %s\n", message); }
static unsigned long priority_of(id queue) { return read_priority(queue); }

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        id<MTLCommandQueue> early = [device newCommandQueue]; // Created before the hooks, like Unity's.
        pg3d_metal_tuning_start();
        assert(priority_supported);
        id<MTLCommandQueue> later = [device newCommandQueue];
        assert(priority_of(early) == 1 && priority_of(later) == 1);

        pg3d_metal_tuning_apply(); // Nothing asked for: nothing changes.
        (void)[early commandBuffer];
        assert(priority_of(early) == 1);
        OptionStatus *gpu = &pg3d_option_status[OPT_GPU_PRIORITY];
        assert(gpu->state == OPT_STATE_GAME);

        pg3d_options[OPT_GPU_PRIORITY] = 1;
        pg3d_metal_tuning_apply();
        (void)[early commandBuffer];
        (void)[later commandBufferWithUnretainedReferences];
        assert(priority_of(early) == 0 && priority_of(later) == 0);
        pg3d_metal_tuning_apply();
        assert(gpu->state == OPT_STATE_APPLIED && gpu->game == 1 && gpu->current == 0);

        // Work still runs at high priority.
        id<MTLCommandBuffer> buffer = [early commandBuffer];
        [buffer commit];
        [buffer waitUntilCompleted];
        assert(buffer.status == MTLCommandBufferStatusCompleted);

        pg3d_options[OPT_GPU_PRIORITY] = PG3D_GAME_VALUE;
        pg3d_metal_tuning_apply();
        (void)[early commandBuffer];
        (void)[later commandBufferWithDescriptor:[MTLCommandBufferDescriptor new]];
        assert(priority_of(early) == 1 && priority_of(later) == 1); // Restored.
        pg3d_metal_tuning_apply();
        assert(gpu->state == OPT_STATE_GAME);
    }
    puts("PASS: GPU priority raise/restore on queues made before and after start (all three command buffer calls), work completes at high priority.");
}
