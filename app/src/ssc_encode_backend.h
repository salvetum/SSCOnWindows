/*
 * SSC Encoder Backend Interface
 *
 * Faz 6a abstraction: the closed-source Samsung SSC encoder (the
 * libScalable_Encoder.so blob, executed via a WSL2/qemu or native Qiling
 * daemon) is reached only through this narrow interface. A future open
 * implementation (Faz 6b clean-room) can be swapped in as a drop-in by
 * providing another SscEncodeBackend, without touching SscEncoder or its
 * callers.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SSC_ENCODE_BACKEND_H
#define SSC_ENCODE_BACKEND_H

#include <cstddef>
#include <cstdint>

class SscEncodeBackend {
public:
    virtual ~SscEncodeBackend() = default;

    /* Configure and (re)start the underlying encoder engine for the given
     * stream. Sample rate is the encoder input rate (48000, or 96000 for
     * SSC UHQ); bitrate is in bits/sec (already snapped to a valid set).
     * Returns false if the engine cannot run (missing blob/daemon). */
    virtual bool init(uint32_t sample_rate, uint32_t channels,
                      uint32_t bitrate_bps) = 0;

    /* Encode one frame of `frames` interleaved int32 PCM samples
     * (SSC 24-bit scale, 2^29) into `out`. On success returns true and sets
     * *out_size. On a recoverable transport/engine failure returns false
     * (caller should recover() then retry). */
    virtual bool encode(const int32_t *pcm, size_t frames, size_t channels,
                        uint8_t *out, size_t out_cap, size_t *out_size) = 0;

    /* Restart the encoder engine and reconnect (rate-limited; watchdog).
     * Returns true if the engine is usable again. */
    virtual bool recover() = 0;

    /* Tear down the encoder engine (graceful daemon shutdown when supported). */
    virtual void shutdown() = 0;

    /* True while the engine is connected/usable. */
    virtual bool connected() const = 0;
};

#endif /* SSC_ENCODE_BACKEND_H */