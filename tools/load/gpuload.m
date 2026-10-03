// Keeps the GPU busy with compute work (about busy_ms per command buffer, then idle for idle_ms), until killed.
#import <Metal/Metal.h>
#include <stdlib.h>
#include <unistd.h>
static const char *src = "#include <metal_stdlib>\nusing namespace metal;\n"
"kernel void busy(device float *o [[buffer(0)]], constant int &n [[buffer(1)]], uint i [[thread_position_in_grid]]) {"
"  float x = i; for (int k = 0; k < n; ++k) x = fma(x, 1.0001f, 0.5f); o[i] = x; }";
int main(int argc, char **argv) {
  @autoreleasepool {
    int iterations = argc > 1 ? atoi(argv[1]) : 5000; int idle_us = argc > 2 ? atoi(argv[2]) : 4000;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&e];
    id<MTLComputePipelineState> busy = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"busy"] error:&e];
    id<MTLBuffer> buf = [dev newBufferWithLength:4 << 22 options:MTLResourceStorageModePrivate];
    id<MTLCommandQueue> q = [dev newCommandQueue];
    for (;;) @autoreleasepool {
      id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
      [enc setComputePipelineState:busy]; [enc setBuffer:buf offset:0 atIndex:0]; [enc setBytes:&iterations length:4 atIndex:1];
      [enc dispatchThreads:MTLSizeMake(1 << 20, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)]; [enc endEncoding];
      [cb commit]; [cb waitUntilCompleted];
      if (idle_us) usleep(idle_us);
    }
  }
}
