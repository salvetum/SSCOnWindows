/*
 * Audio Resampler — 2x upsampling for SSC UHQ
 *
 * Cubic Hermite interpolation: for each pair of input frames, outputs
 * the original frame plus an interpolated midpoint. Uses the Catmull-Rom
 * cubic at t=0.5: out = (9*(a+b) - (prev+next)) / 16.
 * Boundary frames use linear interpolation.
 *
 * Input/output: interleaved float32 stereo (L, R, L, R, ...).
 *
 * SPDX-License-Identifier: MIT
 */

#include "resampler.h"

uint32_t upsample_2x_stereo_f32(const float *in, uint32_t in_frames,
                                 float *out, uint32_t out_cap) {
    if (!in || !out || in_frames < 2) return 0;
    if (out_cap < in_frames * 2) return 0;

    constexpr int CH = 2;
    uint32_t out_frames = in_frames * 2;

    /* Pass 1: even output frames = direct copy of input frames */
    for (uint32_t n = 0; n < in_frames; n++) {
        out[n * 2 * CH + 0] = in[n * CH + 0];
        out[n * 2 * CH + 1] = in[n * CH + 1];
    }

    /* Pass 2: odd output frames = interpolated midpoints
     *
     * Output frame 2n+1 sits between input frames n and n+1.
     * Cubic Hermite at t=0.5 (Catmull-Rom tangent):
     *   mid = (9*(cur + next) - (prev + next_next)) / 16
     * = 0.5625*(cur+next) - 0.0625*(prev+next_next)
     *
     * Boundary (first/last midpoint): linear interpolation. */
    auto lerp = [](float a, float b) { return (a + b) * 0.5f; };

    /* First midpoint: between frame 0 and 1 → linear */
    {
        uint32_t m = 1;
        out[m * CH + 0] = lerp(in[0], in[CH + 0]);
        out[m * CH + 1] = lerp(in[1], in[CH + 1]);
    }

    /* Interior midpoints: between frame n and n+1, n=1..in_frames-3
     * (requires access to n+2, so n+2 < in_frames). */
    for (uint32_t n = 1; n + 2 < in_frames; n++) {
        uint32_t m = n * 2 + 1;
        const float prev_l  = in[(n - 1) * CH + 0];
        const float prev_r  = in[(n - 1) * CH + 1];
        const float cur_l   = in[n * CH + 0];
        const float cur_r   = in[n * CH + 1];
        const float next_l  = in[(n + 1) * CH + 0];
        const float next_r  = in[(n + 1) * CH + 1];
        const float nn_l    = in[(n + 2) * CH + 0];
        const float nn_r    = in[(n + 2) * CH + 1];

        out[m * CH + 0] = (9.0f * (cur_l + next_l) - (prev_l + nn_l)) * 0.0625f;
        out[m * CH + 1] = (9.0f * (cur_r + next_r) - (prev_r + nn_r)) * 0.0625f;
    }

    /* Last midpoint: between frame in_frames-2 and in_frames-1 → linear */
    if (in_frames >= 3) {
        uint32_t m = (in_frames - 2) * 2 + 1;
        uint32_t i1 = (in_frames - 2) * CH;
        uint32_t i2 = (in_frames - 1) * CH;
        out[m * CH + 0] = lerp(in[i1 + 0], in[i2 + 0]);
        out[m * CH + 1] = lerp(in[i1 + 1], in[i2 + 1]);
    }

    /* Effective output: 2*in_frames-1 real frames (originals + midpoints).
     * out_cap guards the full 2*in_frames slots so the caller can allocate
     * in_frames*2; we only claim what we actually filled. */
    return out_frames - 1;
}
