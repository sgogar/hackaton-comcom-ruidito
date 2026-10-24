#pragma once

/*
    Minimal, portable radix-2 FFT (plain C, no dependencies).

    This exists so the DSP pipeline is identical on the PC test bench and
    on the board. On the ESP32-S3 you can swap this for ESP-DSP's
    dsps_fft2r_fc32 for ~5-10x speed; the surrounding feature code only
    needs the magnitude spectrum, so the replacement is localized to
    features.c. For a 512-point frame every 32 ms this plain-C version is
    already comfortably real-time on the S3 (see docs/README.md budget).
*/

/* In-place complex FFT. re/im hold n interleaved as separate arrays.
   n must be a power of two. Forward transform (no normalization). */
void fft_radix2(float *re, float *im, int n);

/* Convenience: real input -> magnitude spectrum (length n/2 + 1).
   `window` may be NULL (rectangular). Does not modify `in`. */
void fft_magnitude(const float *in, const float *window, int n, float *mag_out);
