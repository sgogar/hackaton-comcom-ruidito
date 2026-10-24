#include "fft.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Radix-2 Cooley-Tukey iterativo, in-place, con reordenamiento bit-reversal. */
void fft_radix2(float *re, float *im, int n)
{
    /* reordenamos por bit-reversal */
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
    /* Usamos buffers en el stack asi no reservamos memoria y la funcion queda
       reentrante. En mag_out solo entran n/2+1 valores, asi que laburamos
       aparte en re/im. Ojo que esto se apoya en que n es chico (n<=1024),
       si no, el frame se te va mucho. */
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
