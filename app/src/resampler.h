/*
 * Audio Resampler — 2x upsampling for SSC UHQ
 *
 * Converts float32 stereo audio from 48kHz to 96kHz using cubic
 * Hermite interpolation. Input and output are interleaved float32.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RESAMPLER_H
#define RESAMPLER_H

#include <cstdint>

/* 2x upsample float32 stereo (48k→96k).
 * in_frames: number of input frames (each frame = 2 samples for stereo)
 * Returns in_frames*2 - 1 frames: the N original samples at even indices
 * plus N-1 cubic-interpolated midpoints at odd indices.
 * out_cap must be >= in_frames*2 (allocated for the worst case). */
uint32_t upsample_2x_stereo_f32(const float *in, uint32_t in_frames,
                                 float *out, uint32_t out_cap);

#endif /* RESAMPLER_H */
