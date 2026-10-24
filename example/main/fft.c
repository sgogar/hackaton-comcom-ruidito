#include "fft.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Iterative in-place radix-2 Cooley-Tukey with bit-reversal permutation. */
void fft_radix2(float *re, float *im, int n)
{
    /* bit-reversal reorder */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float wlr = cosf(ang), wli = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k;
                int b = i + k + len / 2;
                float ur = re[a], ui = im[a];
                float vr = re[b] * wr - im[b] * wi;
                float vi = re[b] * wi + im[b] * wr;
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
                float nwr = wr * wlr - wi * wli;
                wi = wr * wli + wi * wlr;
                wr = nwr;
            }
        }
    }
}

void fft_magnitude(const float *in, const float *window, int n, float *mag_out)
{
    /* Local scratch kept on the caller's stack via VLA-free fixed path:
       we write into mag_out space is only n/2+1, so use a small static-free
       approach by transforming in place on temp buffers provided by caller?
       To stay allocation-free and reentrant we require the caller's frame to
       be modest; n<=1024 here. Use stack buffers. */
    float re[n];
    float im[n];
    for (int i = 0; i < n; i++) {
        float w = window ? window[i] : 1.0f;
        re[i] = in[i] * w;
        im[i] = 0.0f;
    }
    fft_radix2(re, im, n);
    int bins = n / 2 + 1;
    for (int k = 0; k < bins; k++) {
        mag_out[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }
}
