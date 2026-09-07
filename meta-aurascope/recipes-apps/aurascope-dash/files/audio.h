#ifndef AUDIO_H
#define AUDIO_H

#include <pthread.h>

#define FFT_SIZE    1024
#define WAVE_POINTS 256
#define SPEC_BARS   64

typedef struct {
    long  ts_ms;
    float rms_db;
    float peak_db;
    float peak_hold_db;
    float wave[WAVE_POINTS];
    float spec[SPEC_BARS];
} audio_snapshot_t;

extern pthread_mutex_t   g_snapshot_lock;
extern audio_snapshot_t  g_snapshot;

/* Starts the ALSA capture thread (detached). Returns 0 on success. */
int audio_capture_start(const char *device);

#endif
