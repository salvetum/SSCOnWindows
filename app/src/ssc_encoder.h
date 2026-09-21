/*
 * SSC Encoder Wrapper
 *
 * Samsung Scalable Codec encoder facade. Concrete encoding is delegated to
 * an SscEncodeBackend (see ssc_encode_backend.h). The default backend —
 * DaemonSscBackend — sends PCM over TCP to the "sscblobd" daemon running
 * inside WSL2 (or natively under Qiling with --ssc-native), which encodes
 * via the real Samsung libScalable_Encoder.so (under qemu-aarch64) and
 * returns the encoded SSC frame.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SSC_ENCODER_H
#define SSC_ENCODER_H

#include "audio_encoder.h"
#include "ssc_encode_backend.h"
#include <cstdint>
#include <memory>

class SscEncoder : public AudioEncoder {
public:
    SscEncoder();
    ~SscEncoder() override;

    AudioCodec codec_type() const override { return AudioCodec::SSC; }
    const char *codec_name() const override { return "SSC"; }

    bool init(uint16_t mtu, EncoderQuality quality,
              uint32_t sample_rate, uint32_t channels) override;

    bool encode(const uint8_t *pcm_data, uint32_t pcm_bytes,
                uint8_t *out_data, uint32_t *out_size,
                uint32_t *out_frames) override;

    uint32_t get_pcm_frames_per_encode() const override { return 864; }

    uint32_t get_bitrate_kbps() const override { return bitrate_kbps_; }

    /* Override the auto bitrate picked from quality (0 = auto). Call before init(). */
    void set_bitrate_override(uint32_t kbps) { bitrate_override_kbps_ = kbps; }

    /* Snap a requested kbps to the nearest bitrate valid for the given sample
     * rate. SSC bitrates are mode-gated: 48 kHz (basic) allows 88..328 kbps,
     * 96 kHz (UHQ) allows 152..886 kbps. Anything else makes the blob emit
     * garbage. 0 (= auto) is returned unchanged. */
    static uint32_t snap_bitrate_kbps(uint32_t kbps, uint32_t sample_rate);

    /* Use the Windows-native Qiling daemon (tools\\ssc_daemon\\sscblobd.py)
     * instead of the WSL2/qemu daemon. Call before init(). */
    void set_native_daemon(bool enabled) { native_daemon_ = enabled; }

    /* Watchdog: restart the daemon and reconnect after a transport
     * failure. Internally triggered on encode() failures and rate-limited
     * (5 s cooldown). Public for diagnostics/telemetry. */
    bool recover();

    /* Number of successful daemon recoveries (watchdog resets). */
    uint32_t get_recovery_count() const { return recovery_count_; }

    void shutdown() override;

private:
    /* Pick a bitrate for the given sample rate + quality. */
    uint32_t pick_bitrate(EncoderQuality quality, uint32_t sample_rate) const;

    bool initialized_ = false;
    bool native_daemon_ = false;
    uint32_t bitrate_kbps_ = 0;
    uint32_t bitrate_override_kbps_ = 0;
    uint32_t channels_ = 2;
    uint64_t next_recovery_ms_ = 0;
    uint32_t recovery_count_ = 0;

    std::unique_ptr<SscEncodeBackend> backend_;
};

#endif /* SSC_ENCODER_H */