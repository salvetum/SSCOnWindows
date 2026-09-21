/*
 * SSC Encoder Wrapper
 *
 * SPDX-License-Identifier: MIT
 */

#include "ssc_encoder.h"
#include "codec_policy.h"
#include "ssc_daemon_backend.h"

#include <cstdio>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr uint32_t kFrameSamples = 864;
constexpr uint32_t kMaxEncodeBytes = 4096;
constexpr uint64_t kRecoveryCooldownMs = 5000;

/* SSC bitrate mode-gating, auto bitrate pick, and core decision helpers now
 * live in codec_policy.h (shared with the CLI/service and unit-tested in
 * tests/unit/codec_policy_tests.cpp). This file only wraps the encoder API. */

} // namespace

SscEncoder::SscEncoder() = default;

SscEncoder::~SscEncoder() {
    shutdown();
}

uint32_t SscEncoder::pick_bitrate(EncoderQuality quality, uint32_t sample_rate) const {
    return codec_policy::pick_bitrate_bps(quality, sample_rate);
}

uint32_t SscEncoder::snap_bitrate_kbps(uint32_t kbps, uint32_t sample_rate) {
    if (kbps == 0) return 0;            /* 0 = auto, leave untouched */
    return codec_policy::snap_bitrate_bps(kbps * 1000, sample_rate) / 1000;
}

bool SscEncoder::recover() {
#ifdef _WIN32
    uint64_t now = GetTickCount64();
    if (now < next_recovery_ms_) {
        return backend_ ? backend_->connected() : false; /* cooldown: back off */
    }
    next_recovery_ms_ = now + kRecoveryCooldownMs;

    if (!backend_) return false;

    fprintf(stderr, "SSC: daemon lost, restarting (recovery #%u)...\n",
            recovery_count_ + 1);
    bool ok = backend_->recover();
    if (ok) {
        recovery_count_++;
        fprintf(stderr, "SSC: daemon recovered (total recoveries=%u)\n",
                recovery_count_);
    } else {
        fprintf(stderr, "SSC: daemon recovery failed, retry in %llu ms\n",
                static_cast<unsigned long long>(kRecoveryCooldownMs));
    }
    return ok;
#else
    return false;
#endif
}

bool SscEncoder::init(uint16_t mtu, EncoderQuality quality,
                      uint32_t sample_rate, uint32_t channels) {
    (void)mtu;
    if (initialized_) shutdown();

    channels_ = (channels == 1) ? 1 : 2;
    uint32_t rate = (sample_rate == 0) ? 48000 : sample_rate;
    uint32_t br;
    if (bitrate_override_kbps_ > 0) {
        uint32_t req = bitrate_override_kbps_ * 1000;
        br = codec_policy::snap_bitrate_bps(req, rate);
        if (br != req) {
            fprintf(stderr,
                    "SSC: bitrate %u bps not valid for %u Hz (mode-gated); "
                    "snapped to %u bps\n", req, rate, br);
        }
    } else {
        br = pick_bitrate(quality, sample_rate);
    }
    bitrate_kbps_ = br / 1000;
    sample_rate = rate;

    backend_ = std::make_unique<DaemonSscBackend>(native_daemon_);
    if (!backend_->init(sample_rate, channels_, br)) {
        backend_.reset();
        return false;
    }
    initialized_ = true;
    fprintf(stderr, "SSC: initialized rate=%u ch=%u bitrate=%u kbps=%u\n",
            sample_rate, channels_, br, bitrate_kbps_);
    return true;
}

bool SscEncoder::encode(const uint8_t *pcm_data, uint32_t pcm_bytes,
                        uint8_t *out_data, uint32_t *out_size,
                        uint32_t *out_frames) {
    if (!initialized_) return false;
    if (!out_size) return false;
    *out_size = 0;
    if (out_frames) *out_frames = 0;

    if (!backend_ || !backend_->connected()) {
        if (!recover()) return false;
    }

    uint32_t expect_bytes = kFrameSamples * channels_ * sizeof(int32_t);
    if (pcm_bytes < expect_bytes) {
        return true;            /* not enough data yet, not an error */
    }

    if (!out_data) return false;

    size_t out_len = 0;
    bool ok = backend_->encode(reinterpret_cast<const int32_t *>(pcm_data),
                               kFrameSamples, channels_,
                               out_data, kMaxEncodeBytes, &out_len);
    if (!ok) {
        *out_size = 0;
        return false;
    }

    *out_size = static_cast<uint32_t>(out_len);
    if (out_frames) *out_frames = 1;
    return true;
}

void SscEncoder::shutdown() {
    if (backend_) {
        backend_->shutdown();
        backend_.reset();
    }
    initialized_ = false;
}