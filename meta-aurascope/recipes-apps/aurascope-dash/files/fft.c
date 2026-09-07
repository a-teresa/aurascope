#include "fft.h"

#include <math.h>

static size_t reverse_bits(size_t x, int bits) {
    size_t r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}

void fft_forward(cplx *buf, size_t n) {
    int bits = 0;
    while (((size_t)1 << bits) < n)
        bits++;

    for (size_t i = 0; i < n; i++) {
        size_t j = reverse_bits(i, bits);
        if (j > i) {
            cplx t = buf[i];
            buf[i] = buf[j];
            buf[j] = t;
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        cplx wlen = { cosf(ang), sinf(ang) };

        for (size_t i = 0; i < n; i += len) {
            cplx w = { 1.0f, 0.0f };

            for (size_t j = 0; j < len / 2; j++) {
                cplx u = buf[i + j];
                cplx v = buf[i + j + len / 2];
                cplx vw = {
                    v.re * w.re - v.im * w.im,
                    v.re * w.im + v.im * w.re,
                };

                buf[i + j].re          = u.re + vw.re;
                buf[i + j].im          = u.im + vw.im;
                buf[i + j + len / 2].re = u.re - vw.re;
                buf[i + j + len / 2].im = u.im - vw.im;

                float nw_re = w.re * wlen.re - w.im * wlen.im;
                float nw_im = w.re * wlen.im + w.im * wlen.re;
                w.re = nw_re;
                w.im = nw_im;
            }
        }
    }
}

void fft_hann_window(float *samples, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1)));
        samples[i] *= w;
    }
}
