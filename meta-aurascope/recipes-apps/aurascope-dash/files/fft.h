#ifndef FFT_H
#define FFT_H

#include <stddef.h>

typedef struct {
    float re;
    float im;
} cplx;

/* In-place iterative radix-2 Cooley-Tukey FFT. n must be a power of two. */
void fft_forward(cplx *buf, size_t n);

/* Applies a Hann window to samples in place. */
void fft_hann_window(float *samples, size_t n);

#endif
