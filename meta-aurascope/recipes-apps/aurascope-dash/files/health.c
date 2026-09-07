#include "health.h"

#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

pthread_mutex_t   g_health_lock = PTHREAD_MUTEX_INITIALIZER;
health_snapshot_t g_health;

/* Bare markers only: aurascope-dash stamps its own receive time for each
 * event rather than trusting ply to format/parse kernel timestamps. */
static const char *PLY_SCRIPT =
    "kprobe:snd_pcm_period_elapsed { printf(\"P\\n\"); }\n"
    "kprobe:__snd_pcm_xrun { printf(\"X\\n\"); }\n";

static const char *PLY_SCRIPT_PATH = "/tmp/aurascope-health.ply";

static pid_t g_ply_pid = -1;
static long  g_start_ms;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void handle_term(int sig) {
    (void)sig;
    if (g_ply_pid > 0)
        kill(g_ply_pid, SIGTERM);
    _exit(0);
}

static int write_script(void) {
    FILE *f = fopen(PLY_SCRIPT_PATH, "w");
    if (!f)
        return -1;
    fputs(PLY_SCRIPT, f);
    fclose(f);
    return 0;
}

/* bucket i covers [2^(i-1), 2^i) ms; the last bucket catches everything
 * at or above 2^(PERIOD_HIST_BUCKETS-2) ms. */
static int bucket_for_ms(long ms) {
    int  b = 0;
    long edge = 1;
    while (ms >= edge && b < PERIOD_HIST_BUCKETS - 1) {
        edge <<= 1;
        b++;
    }
    return b;
}

static void record_period(long ts_ms, long *prev_ms) {
    if (*prev_ms >= 0) {
        long delta = ts_ms - *prev_ms;
        if (delta < 0)
            delta = 0;
        int b = bucket_for_ms(delta);

        pthread_mutex_lock(&g_health_lock);
        g_health.period_hist[b]++;
        g_health.ply_ok = 1;
        pthread_mutex_unlock(&g_health_lock);
    }
    *prev_ms = ts_ms;
}

static void record_xrun(long ts_ms) {
    pthread_mutex_lock(&g_health_lock);
    g_health.xrun_total++;
    g_health.xrun_last_ms = ts_ms - g_start_ms;
    pthread_mutex_unlock(&g_health_lock);
}

static void *reader_thread(void *arg) {
    FILE *stream = (FILE *)arg;
    char  line[64];
    long  prev_period_ms = -1;

    while (fgets(line, sizeof(line), stream)) {
        long ts = now_ms();
        if (line[0] == 'P')
            record_period(ts, &prev_period_ms);
        else if (line[0] == 'X')
            record_xrun(ts);
    }

    fclose(stream);
    return NULL;
}

int health_monitor_start(void) {
    memset(&g_health, 0, sizeof(g_health));
    g_health.xrun_last_ms = -1;
    g_start_ms = now_ms();

    if (write_script() != 0) {
        fprintf(stderr, "aurascope-dash: failed to write ply script\n");
        return -1;
    }

    /* ply has no flag to force unbuffered/line-buffered stdout in this
     * version, so give it a pty: glibc line-buffers stdout by default
     * when it's a tty, and fully-buffers (~4KB) otherwise, which would
     * stall single "P\n"/"X\n" events for a very long time on a plain
     * pipe. */
    int master_fd, slave_fd;
    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0) {
        perror("openpty");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        close(master_fd);
        dup2(slave_fd, STDOUT_FILENO);
        close(slave_fd);
        execlp("ply", "ply", PLY_SCRIPT_PATH, (char *)NULL);
        _exit(127); /* only reached if exec fails */
    }

    g_ply_pid = pid;
    close(slave_fd);

    FILE *stream = fdopen(master_fd, "r");
    if (!stream) {
        perror("fdopen");
        return -1;
    }

    signal(SIGTERM, handle_term);
    signal(SIGINT, handle_term);

    pthread_t tid;
    if (pthread_create(&tid, NULL, reader_thread, stream) != 0) {
        fclose(stream);
        return -1;
    }
    pthread_detach(tid);
    return 0;
}
