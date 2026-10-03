# Load generators for contention tests

Used with `tools/option_bench.py` to copy a busy Mac (1.7.0 engine thread and GPU priority tests). Build natively and stop with Ctrl-C or `kill`.

```sh
clang -O2 tools/load/cpuload.c -o /tmp/cpuload && /tmp/cpuload 16 600 400        # 16 threads: 600 µs busy, 400 µs asleep
clang -fobjc-arc -framework Metal -framework Foundation tools/load/gpuload.m -o /tmp/gpuload && /tmp/gpuload 20000 5000   # ~15 ms GPU buffers, 5 ms gaps
```
