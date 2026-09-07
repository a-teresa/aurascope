#ifndef HEALTH_H
#define HEALTH_H

#include <pthread.h>

/* Log2-ish ms buckets: <1, 1-2, 2-4, 4-8, ..., 512-1024, >=1024 */
#define PERIOD_HIST_BUCKETS 12

typedef struct {
    unsigned long period_hist[PERIOD_HIST_BUCKETS];
    unsigned long xrun_total;
    long          xrun_last_ms; /* ms since monitor start at last xrun, -1 if none yet */
    int           ply_ok;       /* set once the first period event is observed */
} health_snapshot_t;

extern pthread_mutex_t   g_health_lock;
extern health_snapshot_t g_health;

/* Spawns ply with kprobes on snd_pcm_period_elapsed and __snd_pcm_xrun, and
 * starts a thread that turns its event stream into the histogram/counters
 * above. Returns 0 on success. */
int health_monitor_start(void);

#endif
