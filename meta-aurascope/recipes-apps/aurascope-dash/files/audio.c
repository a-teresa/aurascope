#include "audio.h"
#include "fft.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

pthread_mutex_t  g_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
audio_snapshot_t g_snapshot;

/* Rolling buffer of the most recent FFT_SIZE mono samples, in [-1, 1]. */
static float  ring[FFT_SIZE];
static size_t ring_len = 0;
static size_t ring_pos = 0;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static float mono_sample(const int16_t *frames, snd_pcm_uframes_t i, unsigned channels) {
    int32_t sum = 0;
    for (unsigned c = 0; c < channels; c++)
        sum += frames[i * channels + c];
    return (float)(sum / (int)channels) / 32768.0f;
}

static void ring_push(const int16_t *frames, snd_pcm_uframes_t n, unsigned channels) {
    for (snd_pcm_uframes_t i = 0; i < n; i++) {
        ring[ring_pos] = mono_sample(frames, i, channels);
        ring_pos = (ring_pos + 1) % FFT_SIZE;
        if (ring_len < FFT_SIZE)
            ring_len++;
    }
}

static void update_levels(const int16_t *frames, snd_pcm_uframes_t n, unsigned channels) {
    float  peak = 0.0f;
    double sq_sum = 0.0;

    for (snd_pcm_uframes_t i = 0; i < n; i++) {
        float s = mono_sample(frames, i, channels);
        float a = fabsf(s);
        if (a > peak)
            peak = a;
        sq_sum += (double)s * (double)s;
    }

    float rms = sqrtf((float)(sq_sum / (double)n));
    float rms_db  = 20.0f * log10f(rms + 1e-9f);
    float peak_db = 20.0f * log10f(peak + 1e-9f);

    pthread_mutex_lock(&g_snapshot_lock);
    g_snapshot.rms_db  = rms_db;
    g_snapshot.peak_db = peak_db;
    if (peak_db > g_snapshot.peak_hold_db)
        g_snapshot.peak_hold_db = peak_db;
    else
        g_snapshot.peak_hold_db -= 0.3f; /* slow decay per period */
    pthread_mutex_unlock(&g_snapshot_lock);
}

static void update_spectrum(void) {
    if (ring_len < FFT_SIZE)
        return;

    static float linear[FFT_SIZE];
    static float windowed[FFT_SIZE];
    static cplx  buf[FFT_SIZE];

    for (size_t i = 0; i < FFT_SIZE; i++)
        linear[i] = ring[(ring_pos + i) % FFT_SIZE];

    memcpy(windowed, linear, sizeof(linear));
    fft_hann_window(windowed, FFT_SIZE);

    for (size_t i = 0; i < FFT_SIZE; i++) {
        buf[i].re = windowed[i];
        buf[i].im = 0.0f;
    }
    fft_forward(buf, FFT_SIZE);

    size_t bins = FFT_SIZE / 2;
    size_t per_bar = bins / SPEC_BARS;
    if (per_bar < 1)
        per_bar = 1;

    float bars[SPEC_BARS];
    for (size_t b = 0; b < SPEC_BARS; b++) {
        size_t start = b * per_bar;
        size_t end = start + per_bar;
        if (end > bins)
            end = bins;

        float  mag_sum = 0.0f;
        size_t count = 0;
        for (size_t i = start; i < end; i++) {
            mag_sum += sqrtf(buf[i].re * buf[i].re + buf[i].im * buf[i].im);
            count++;
        }
        float avg = count ? mag_sum / (float)count : 0.0f;
        bars[b] = 20.0f * log10f(avg / (float)FFT_SIZE + 1e-9f);
    }

    float  wave[WAVE_POINTS];
    size_t stride = FFT_SIZE / WAVE_POINTS;
    for (size_t i = 0; i < WAVE_POINTS; i++)
        wave[i] = linear[i * stride];

    pthread_mutex_lock(&g_snapshot_lock);
    g_snapshot.ts_ms = now_ms();
    memcpy(g_snapshot.wave, wave, sizeof(wave));
    memcpy(g_snapshot.spec, bars, sizeof(bars));
    pthread_mutex_unlock(&g_snapshot_lock);
}

static void *capture_thread(void *arg) {
    const char *device = (const char *)arg;
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hw;
    unsigned channels = 1;
    unsigned rate = 48000;
    snd_pcm_uframes_t period_size = 512;

    int err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "aurascope-dash: cannot open device '%s': %s\n",
                device, snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);

    if (snd_pcm_hw_params_set_channels(pcm, hw, channels) < 0) {
        channels = 2;
        snd_pcm_hw_params_set_channels(pcm, hw, channels);
    }

    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period_size, 0);

    if (snd_pcm_hw_params(pcm, hw) < 0) {
        fprintf(stderr, "aurascope-dash: failed to set hw params on '%s'\n", device);
        snd_pcm_close(pcm);
        return NULL;
    }

    fprintf(stderr, "aurascope-dash: capturing %uch @ %uHz, period=%lu frames\n",
            channels, rate, (unsigned long)period_size);

    int16_t *frames = malloc(period_size * channels * sizeof(int16_t));
    if (!frames) {
        snd_pcm_close(pcm);
        return NULL;
    }

    for (;;) {
        snd_pcm_sframes_t n = snd_pcm_readi(pcm, frames, period_size);
        if (n == -EPIPE) {
            snd_pcm_prepare(pcm);
            continue;
        } else if (n < 0) {
            n = snd_pcm_recover(pcm, (int)n, 1);
            if (n < 0) {
                fprintf(stderr, "aurascope-dash: read error: %s\n", snd_strerror((int)n));
                break;
            }
            continue;
        }

        update_levels(frames, (snd_pcm_uframes_t)n, channels);
        ring_push(frames, (snd_pcm_uframes_t)n, channels);
        update_spectrum();
    }

    free(frames);
    snd_pcm_close(pcm);
    return NULL;
}

int audio_capture_start(const char *device) {
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.peak_hold_db = -120.0f;

    pthread_t tid;
    if (pthread_create(&tid, NULL, capture_thread, (void *)device) != 0)
        return -1;
    pthread_detach(tid);
    return 0;
}
