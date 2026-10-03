// N threads at default priority, each busy for busy_us then asleep for sleep_us, until killed.
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach/mach_time.h>
static int busy_us, sleep_us; static volatile double sink;
static double now_us(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * (double)tb.numer / tb.denom / 1000.0; }
static void *spin(void *p) { (void)p; for (;;) { double t = now_us(); while (now_us() - t < busy_us) sink += 1; usleep(sleep_us); } }
int main(int argc, char **argv) {
  int n = argc > 1 ? atoi(argv[1]) : 12; busy_us = argc > 2 ? atoi(argv[2]) : 500; sleep_us = argc > 3 ? atoi(argv[3]) : 500;
  pthread_t t; for (int i = 0; i < n; ++i) pthread_create(&t, NULL, spin, NULL);
  pause();
}
