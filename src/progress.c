#define _POSIX_C_SOURCE 199309L
#include "progress.h"
#include <stdio.h>
#include <time.h>
#include <stdint.h>

static uint64_t g_total = 0;
static struct timespec g_start;

static const char *spinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static int spin = 0;

static double elapsed_sec(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - g_start.tv_sec) + (now.tv_nsec - g_start.tv_nsec) / 1e9;
}

void progress_start(uint64_t total) {
    g_total = total;
    clock_gettime(CLOCK_MONOTONIC, &g_start);
}

void progress_update(uint64_t processed) {
    double elapsed = elapsed_sec();
    double percent = g_total ? (double)processed / g_total : 0;
    double speed = elapsed > 0 ? processed / elapsed : 0;
    double mb = speed / (1024*1024);

    int bar = (int)(percent * 30);

    printf("\r%s [", spinner[spin++ % 10]);
    for (int i = 0; i < 30; i++) {
        printf("%s", i < bar ? "█" : "░");
    }

    printf("] %6.2f%% %6.2f MB/s", percent*100, mb);
    fflush(stdout);
}

void progress_finish(void) {
    printf("\n");
}
