/*
 * codec_policy.h - pure codec/bitrate/sample-rate decision helpers.
 *
 * The A2DP codec selection rules used to be duplicated inline in the CLI
 * (main.cpp) and the service (a2dp_service.cpp), and the SSC bitrate tables
 * lived only inside ssc_encoder.cpp. This header centralizes the *policy* into
 * pure, dependency-free inline functions so the GUI/CLI/service share one rule
 * set and so the rules can be unit-tested in isolation (tests/unit/*).
 *
 * Plan: docs/dev/PLAN_PROFESSIONALIZATION.md Faz 4 (unit tests for codec
 * fallback, bitrate snapping, UHQ capability-bit fallback).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SSCONWINDOWS_CODEC_POLICY_H
#define SSCONWINDOWS_CODEC_POLICY_H

#include "audio_encoder.h"

#include <cstdint>

namespace codec_policy {

/* Remote codec advertisement (BTstack parsec of the AVDTP capability info).
 * Also convertible from the transport's RemoteCodecCaps at call sites. */
struct Caps {
    bool sbc = false;
    bool aac = false;
    bool ssc = false;
    uint8_t ssc_cap = 0;     /* raw SSC capability byte (0x02 = UHQ2/96k) */
    bool ssc_uhq = false;    /* remote advertised the UHQ2/96k bit */
};

/* Return whether the remote advertises the given codec. */
inline bool codec_present(AudioCodec codec, const Caps &caps) {
    switch (codec) {
    case AudioCodec::SSC: return caps.ssc;
    case AudioCodec::AAC: return caps.aac;
    case AudioCodec::SBC: return caps.sbc;
    }
    return false;
}

/* Select the codec to use. Honors `requested` when advertised, otherwise
 * falls back in priority order SSC > AAC > SBC. Returns true and sets *out
 * when a usable codec exists. */
inline bool resolve_codec(AudioCodec requested, const Caps &caps, AudioCodec *out) {
    if (codec_present(requested, caps)) {
        if (out) *out = requested;
        return true;
    }
    if (caps.ssc) { if (out) *out = AudioCodec::SSC; return true; }
    if (caps.aac) { if (out) *out = AudioCodec::AAC; return true; }
    if (caps.sbc) { if (out) *out = AudioCodec::SBC; return true; }
    return false;
}

/* SSC bitrates are mode-gated (openssc pipewire/a2dp-codec-ssc.c). The 48 kHz
 * basic mode (SSC_CAP_BASIC_48K = 0x0C) only permits the "basic" set; the UHQ
 * bitrates require the UHQ2 (0x02) capability bit. Feeding any other value to
 * the blob makes it emit a malformed frame -> garbled audio. Keep sorted
 * ascending. */
inline constexpr uint32_t kBasicBitratesBps[] = {
    88000, 96000, 128000, 192000, 229000, 256000, 328000,
};
inline constexpr uint32_t kUhqBitratesBps[] = {
    152000, 250000, 291000, 308000, 442000, 584000, 886000,
};

/* SSC UHQ rates encode at 96 kHz (2x SRC from the 48 kHz capture). */
inline bool is_uhq_rate(uint32_t sample_rate) {
    return sample_rate == 88200 || sample_rate == 96000;
}

/* Snap a requested bitrate (bps) to the nearest value the blob accepts at the
 * given sample rate. */
inline uint32_t snap_bitrate_bps(uint32_t bps, uint32_t sample_rate) {
    const bool uhq = is_uhq_rate(sample_rate);
    const uint32_t *list = uhq ? kUhqBitratesBps : kBasicBitratesBps;
    const size_t n = uhq ? (sizeof(kUhqBitratesBps) / sizeof(kUhqBitratesBps[0]))
                         : (sizeof(kBasicBitratesBps) / sizeof(kBasicBitratesBps[0]));
    uint32_t best = list[0];
    uint32_t best_diff = (bps > best) ? (bps - best) : (best - bps);
    for (size_t i = 1; i < n; ++i) {
        const uint32_t cand = list[i];
        const uint32_t diff = (bps > cand) ? (bps - cand) : (cand - bps);
        if (diff < best_diff) {
            best_diff = diff;
            best = cand;
        }
    }
    return best;
}

/* Auto bitrate (bps) for a given quality and sample rate (SSC). */
inline uint32_t pick_bitrate_bps(EncoderQuality quality, uint32_t sample_rate) {
    const bool uhq = is_uhq_rate(sample_rate);
    switch (quality) {
    case EncoderQuality::High:
        return uhq ? 584000 : 229000;   /* UHQ high / SSC 229k */
    case EncoderQuality::Standard:
        return uhq ? 442000 : 192000;   /* UHQ std / SSC 192k */
    case EncoderQuality::Mobile:
    default:
        return uhq ? 250000 : 128000;   /* UHQ low / SSC 128k */
    }
}

/* Effective encode sample rate after the SSC UHQ capability-bit gate.
 * A 96 kHz UHQ request on a device without the UHQ2 (0x02) bit would be
 * accepted by SET_CONFIGURATION but yield silence (Buds3 FE cap=0x3C), so it
 * is dropped to the capture rate. `requested_sr` is the UHQ rate the user
 * asked for (88200/96000). Non-SSC codecs and non-UHQ requests pass through
 * unchanged. `fell_back` (optional) is set true when UHQ was dropped. */
inline uint32_t resolve_encode_sr(AudioCodec codec, uint32_t capture_sr,
                                  uint32_t requested_sr, const Caps &caps,
                                  bool *fell_back = nullptr) {
    if (fell_back) *fell_back = false;
    if (codec == AudioCodec::SSC && capture_sr == 48000 && is_uhq_rate(requested_sr)) {
        if (caps.ssc_uhq) return requested_sr;
        if (fell_back) *fell_back = true;
    }
    return capture_sr;
}

/* Compose the full A2DP stream configuration decision (codec + encode sample
 * rate). Wire the transport's caps into this when opening a stream. */
inline bool resolve_stream(AudioCodec requested, uint32_t capture_sr,
                           uint32_t requested_sr, const Caps &caps,
                           AudioCodec *codec_out, uint32_t *sr_out,
                           bool *codec_fell_back = nullptr,
                           bool *uhq_fell_back = nullptr) {
    if (codec_fell_back) *codec_fell_back = false;
    if (uhq_fell_back) *uhq_fell_back = false;
    if (!resolve_codec(requested, caps, codec_out)) return false;
    if (codec_out && *codec_out != requested && codec_fell_back)
        *codec_fell_back = true;
    if (sr_out)
        *sr_out = resolve_encode_sr(*codec_out, capture_sr, requested_sr, caps,
                                    uhq_fell_back);
    return true;
}

} /* namespace codec_policy */

#endif /* SSCONWINDOWS_CODEC_POLICY_H */