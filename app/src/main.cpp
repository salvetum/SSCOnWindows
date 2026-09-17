/*
 * A2DP Windows Bridge (A2DPWB): Bluetooth Audio Streaming Application
 *
 * Supports multiple Bluetooth audio codecs:
 *   - SSC (Samsung Scalable Codec)
 *   - AAC (MPEG-2/4 AAC-LC, up to 256 kbps)
 *   - SBC (mandatory A2DP codec, up to ~345 kbps)
 *
 * Main entry point. Orchestrates:
 *   1. Bluetooth device discovery and selection
 *   2. WASAPI loopback audio capture
 *   3. Audio encoding via selected codec
 *   4. AVDTP signaling and media streaming via BTstack + WinUSB
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <windows.h>

#include "audio_encoder.h"
#include "audio_device_enum.h"
#include "capture_mode.h"
#include "wasapi_capture.h"
#include "a2dp_sbc_encoder.h"
#include "aac_encoder.h"
#include "ssc_encoder.h"
#include "bt_device.h"
#include "btstack_transport.h"
#include "bt_adapter_enum.h"
#include "config_path.h"
#include "resampler.h"
#include "wx_app.h"

/* Global state */
static std::atomic<bool> g_running{true};
static std::mutex g_encode_mutex;

/* Audio buffer for float32->PCM conversion (sized per encoder sample width) */
static std::vector<uint8_t> g_pcm_buffer;
static std::vector<uint8_t> g_encode_buffer;

/* Residual PCM buffer: leftover samples from previous WASAPI callback
 * that didn't fill a complete encoder frame (e.g. 480 samples / 128 per frame
 * = 3 frames + 96 leftover). Without this, 20% of audio data is lost. */
static std::vector<uint8_t> g_pcm_residual;

/* Streaming components */
static std::atomic<AudioEncoder *> g_encoder{nullptr};
static AudioCodec g_active_codec = AudioCodec::SSC;
static uint32_t g_timestamp = 0;
static uint32_t g_active_channels = 2;
static uint32_t g_encoder_sample_bytes = 2; /* 2 for int16, 4 for int32 */
static double g_pcm_int32_scale = 2147483647.0; /* int32 scale (SSC daemon expects 2^29) */
static uint32_t g_encode_sample_rate = 0; /* 0 = same as capture; 96000 = SSC UHQ */
static bool g_ssc_native_daemon = false; /* --ssc-native: Windows Qiling daemon */
static uint32_t g_ssc_bitrate_kbps = 0;  /* --bitrate: explicit SSC kbps (0 = auto) */

/* BTstack transport */
static std::atomic<BtStackTransport *> g_transport{nullptr};

/* Convert float32 PCM (WASAPI default) to int16 PCM */
static void convert_float32_to_int16(const float *src, int16_t *dst, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
        float s = src[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        dst[i] = static_cast<int16_t>(s * 32767.0f);
    }
}

/* Convert float32 PCM (WASAPI default) to int32 PCM (24/32-bit encoder input).
 * Uses double arithmetic to avoid float32 precision overflow at INT32_MAX. */
static void convert_float32_to_int32(const float *src, int32_t *dst, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
        float s = src[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        dst[i] = static_cast<int32_t>(static_cast<double>(s) * g_pcm_int32_scale);
    }
}

/* Status encoder counters for STATS line */
static uint64_t g_enc_bytes_total = 0;
static uint64_t g_enc_us_total = 0;
static uint32_t g_enc_calls = 0;

/* Audio callback: receives PCM data, encodes, sends via transport */
static uint64_t g_cb_samples_total = 0;
static uint64_t g_cb_bytes_total = 0;
static uint64_t g_cb_rms_total = 0;
static uint64_t g_last_cb_log = 0;

static float compute_rms_sum(const float *s, uint32_t n) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; i++) sum += s[i] * s[i];
    return sum;
}

static float compute_rms_sum_i16(const int16_t *s, uint32_t n) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float v = static_cast<float>(s[i]) / 32768.0f;
        sum += v * v;
    }
    return sum;
}

static void audio_callback(
    const uint8_t *data, uint32_t frames,
    uint32_t channels, uint32_t sample_rate, uint32_t bits_per_sample)
{
    (void)sample_rate;

    /* Health metric: report capture level every 2s */
    {
        uint64_t now_ms = GetTickCount64();
        g_cb_samples_total += frames;
        g_cb_bytes_total += (uint64_t)frames * channels * (bits_per_sample / 8);
        if (bits_per_sample == 32)
            g_cb_rms_total += (uint64_t)(compute_rms_sum(reinterpret_cast<const float *>(data), frames * channels) * 65536.0);
        else if (bits_per_sample == 16)
            g_cb_rms_total += (uint64_t)(compute_rms_sum_i16(reinterpret_cast<const int16_t *>(data), frames * channels) * 65536.0);
        if (now_ms - g_last_cb_log >= 2000) {
            g_last_cb_log = now_ms;
            double rms_avg = g_cb_samples_total
                ? (static_cast<double>(g_cb_rms_total) / 65536.0) / g_cb_samples_total
                : 0.0;
            double db = (rms_avg > 0.0) ? (10.0 * log10(rms_avg)) : -200.0;
            /* Compute arrival rate (frames/sec) since last sample */
            static uint64_t last_frames = 0;
            static uint64_t last_tick = 0;
            double rate = 0.0;
            if (last_tick != 0) {
                double dt = (now_ms - last_tick) / 1000.0;
                if (dt > 0)
                    rate = (g_cb_samples_total - last_frames) / dt;
            }
            last_frames = g_cb_samples_total;
            last_tick = now_ms;
            uint32_t qd = 0, sf = 0;
            BtStackTransport *t = g_transport.load();
            if (t) { qd = t->get_queue_depth(); sf = t->get_and_reset_send_failure_count(); }
            fprintf(stderr, "CAP: frames=%llu rate=%.0ffs bytes=%llu rms_avg=%.6f db=%.1f queue=%u fail=%u\n",
                (unsigned long long)g_cb_samples_total, rate,
                (unsigned long long)g_cb_bytes_total, rms_avg, db, qd, sf);
            /* Per-window STATS: effective bitrate + average encode latency */
            {
                static uint64_t s_last_bytes = 0, s_last_us = 0;
                static uint32_t s_last_calls = 0;
                double dt_stats = 2.0; /* window is 2000 ms */
                double kbps = (g_enc_bytes_total >= s_last_bytes)
                    ? (double)(g_enc_bytes_total - s_last_bytes) * 8.0 / 1000.0 / dt_stats : 0.0;
                double enc_avg_ms = (g_enc_calls > s_last_calls)
                    ? (double)(g_enc_us_total - s_last_us) / 1000.0 / (double)(g_enc_calls - s_last_calls)
                    : 0.0;
                s_last_bytes = g_enc_bytes_total;
                s_last_us = g_enc_us_total;
                s_last_calls = g_enc_calls;
                fprintf(stderr, "STATS: bitrate=%.0fkbps enc_avg=%.2fms enc_calls=%u enc_bytes=%llu\n",
                        kbps, enc_avg_ms, g_enc_calls, (unsigned long long)g_enc_bytes_total);
            }
            fflush(stderr);
        }
    }

    if (!g_running.load()) return;

    LARGE_INTEGER cb_start, cb_end, cb_freq;
    QueryPerformanceCounter(&cb_start);
    QueryPerformanceFrequency(&cb_freq);

    std::lock_guard<std::mutex> lock(g_encode_mutex);

    AudioEncoder *encoder = g_encoder.load();
    if (!encoder) return;

    uint32_t use_channels = g_active_channels;
    uint32_t sb = g_encoder_sample_bytes;
    bool need_downmix = (channels > use_channels);
    uint32_t out_channels = need_downmix ? use_channels : channels;

    /* Convert WASAPI output to encoder's expected integer format.
     * For multichannel, downmix to stereo in float domain BEFORE integer
     * conversion using ITU-R BS.775 coefficients so that center, LFE,
     * and surround channels are mixed in rather than discarded. */
    const uint8_t *pcm_data;

    if (bits_per_sample == 32) {
        /* WASAPI float32 → downmix (if needed) → encoder integer format */
        const float *float_src = reinterpret_cast<const float *>(data);

        static std::vector<float> float_stereo;
        if (need_downmix && use_channels == 2) {
            float_stereo.resize(frames * 2);
            for (uint32_t f = 0; f < frames; f++) {
                const float *ch = float_src + f * channels;
                float fl = ch[0], fr = ch[1];
                float fc  = (channels > 2) ? ch[2] : 0.0f;
                float lfe = (channels > 3) ? ch[3] : 0.0f;
                float bl  = (channels > 4) ? ch[4] : 0.0f;
                float br  = (channels > 5) ? ch[5] : 0.0f;
                float sl  = (channels > 6) ? ch[6] : 0.0f;
                float sr  = (channels > 7) ? ch[7] : 0.0f;
                /* ITU-R BS.775 stereo downmix + LFE at -6dB */
                float_stereo[f * 2 + 0] = fl + 0.707107f * fc
                    + 0.707107f * (bl + sl) + 0.5f * lfe;
                float_stereo[f * 2 + 1] = fr + 0.707107f * fc
                    + 0.707107f * (br + sr) + 0.5f * lfe;
            }
            float_src = float_stereo.data();
        } else if (need_downmix) {
            float_stereo.resize(frames * use_channels);
            for (uint32_t f = 0; f < frames; f++)
                for (uint32_t c = 0; c < use_channels; c++)
                    float_stereo[f * use_channels + c] = float_src[f * channels + c];
            float_src = float_stereo.data();
        }

        /* %% SSC UHQ: 2x upsampling (48 kHz WASAPI capture → 96 kHz encode).
         * Applied on downmixed interleaved float32 (stereo) before the
         * integer conversion and encoding. */
        static std::vector<float> src_float;
        if (g_encode_sample_rate == 96000 && out_channels == 2) {
            uint32_t src_cap = frames * 8; /* 2x frames × 2ch × 4B */
            if (src_float.size() < src_cap) src_float.resize(src_cap);
            uint32_t out_frames = upsample_2x_stereo_f32(float_src, frames,
                                                        src_float.data(), frames * 2);
            frames = out_frames;
            float_src = src_float.data();
        }

        uint32_t out_samples = frames * out_channels;
        uint32_t buf_bytes = out_samples * sb;
        if (g_pcm_buffer.size() < buf_bytes)
            g_pcm_buffer.resize(buf_bytes);
        if (sb == 4) {
            convert_float32_to_int32(
                float_src,
                reinterpret_cast<int32_t *>(g_pcm_buffer.data()),
                out_samples);
        } else {
            convert_float32_to_int16(
                float_src,
                reinterpret_cast<int16_t *>(g_pcm_buffer.data()),
                out_samples);
        }
        pcm_data = g_pcm_buffer.data();
    } else if (bits_per_sample == 16) {
        uint32_t total_samples = frames * channels;
        if (need_downmix && use_channels == 2) {
            /* Downmix int16 multichannel to stereo (+ optional int16→int32 upscale) */
            uint32_t buf_bytes = frames * 2 * sb;
            if (g_pcm_buffer.size() < buf_bytes)
                g_pcm_buffer.resize(buf_bytes);
            const int16_t *src = reinterpret_cast<const int16_t *>(data);
            if (sb == 4) {
                int32_t *dst = reinterpret_cast<int32_t *>(g_pcm_buffer.data());
                for (uint32_t f = 0; f < frames; f++) {
                    const int16_t *ch = src + f * channels;
                    int64_t fl = static_cast<int32_t>(ch[0]) << 16;
                    int64_t fr = static_cast<int32_t>(ch[1]) << 16;
                    int64_t fc  = (channels > 2) ? (static_cast<int32_t>(ch[2]) << 16) : 0;
                    int64_t lfe = (channels > 3) ? (static_cast<int32_t>(ch[3]) << 16) : 0;
                    int64_t bl  = (channels > 4) ? (static_cast<int32_t>(ch[4]) << 16) : 0;
                    int64_t br  = (channels > 5) ? (static_cast<int32_t>(ch[5]) << 16) : 0;
                    int64_t sl  = (channels > 6) ? (static_cast<int32_t>(ch[6]) << 16) : 0;
                    int64_t sr  = (channels > 7) ? (static_cast<int32_t>(ch[7]) << 16) : 0;
                    int64_t l = fl + fc*707/1000 + (bl+sl)*707/1000 + lfe*500/1000;
                    int64_t r = fr + fc*707/1000 + (br+sr)*707/1000 + lfe*500/1000;
                    if (l > INT32_MAX) l = INT32_MAX; if (l < INT32_MIN) l = INT32_MIN;
                    if (r > INT32_MAX) r = INT32_MAX; if (r < INT32_MIN) r = INT32_MIN;
                    dst[f * 2 + 0] = static_cast<int32_t>(l);
                    dst[f * 2 + 1] = static_cast<int32_t>(r);
                }
            } else {
                int16_t *dst = reinterpret_cast<int16_t *>(g_pcm_buffer.data());
                for (uint32_t f = 0; f < frames; f++) {
                    const int16_t *ch = src + f * channels;
                    int32_t fl = ch[0], fr = ch[1];
                    int32_t fc  = (channels > 2) ? ch[2] : 0;
                    int32_t lfe = (channels > 3) ? ch[3] : 0;
                    int32_t bl  = (channels > 4) ? ch[4] : 0;
                    int32_t br  = (channels > 5) ? ch[5] : 0;
                    int32_t sl  = (channels > 6) ? ch[6] : 0;
                    int32_t sr  = (channels > 7) ? ch[7] : 0;
                    int32_t l = fl + fc*707/1000 + (bl+sl)*707/1000 + lfe*500/1000;
                    int32_t r = fr + fc*707/1000 + (br+sr)*707/1000 + lfe*500/1000;
                    if (l > 32767) l = 32767; if (l < -32768) l = -32768;
                    if (r > 32767) r = 32767; if (r < -32768) r = -32768;
                    dst[f * 2 + 0] = static_cast<int16_t>(l);
                    dst[f * 2 + 1] = static_cast<int16_t>(r);
                }
            }
            pcm_data = g_pcm_buffer.data();
        } else if (sb == 4) {
            /* Upscale int16 → int32 for high-bit-depth encoder */
            uint32_t buf_bytes = total_samples * 4;
            if (g_pcm_buffer.size() < buf_bytes)
                g_pcm_buffer.resize(buf_bytes);
            const int16_t *src16 = reinterpret_cast<const int16_t *>(data);
            int32_t *dst32 = reinterpret_cast<int32_t *>(g_pcm_buffer.data());
            for (uint32_t i = 0; i < total_samples; i++)
                dst32[i] = static_cast<int32_t>(src16[i]) << 16;
            pcm_data = g_pcm_buffer.data();
        } else {
            pcm_data = data;
        }
    } else {
        return;
    }

    uint32_t new_pcm_bytes = frames * use_channels * sb;
    uint32_t pcm_frames_per_encode = encoder->get_pcm_frames_per_encode();
    uint32_t bytes_per_encode = pcm_frames_per_encode * use_channels * sb;

    if (g_encode_buffer.size() < 2048) {
        g_encode_buffer.resize(2048);
    }

    /* Combine residual from previous callback with new data.
     * Without this, leftover samples (e.g. 480 % 128 = 96 samples at 48kHz)
     * are lost each callback, causing ~20% audio data loss → slow playback. */
    static std::vector<uint8_t> combined_pcm;
    uint32_t residual_bytes = static_cast<uint32_t>(g_pcm_residual.size());
    uint32_t pcm_bytes = residual_bytes + new_pcm_bytes;
    combined_pcm.resize(pcm_bytes);
    if (residual_bytes > 0) {
        memcpy(combined_pcm.data(), g_pcm_residual.data(), residual_bytes);
    }
    memcpy(combined_pcm.data() + residual_bytes, pcm_data, new_pcm_bytes);
    pcm_data = combined_pcm.data();

    uint32_t offset = 0;

    /*
     * Accumulate encoded frames and send as MTU-sized RTP packets.
     * Multiple frames per packet reduces overhead and ensures all
     * encoded data is sent (not overwritten).
     */
    BtStackTransport *transport = g_transport.load();
    if (!transport) return;

    uint16_t mtu = transport->get_media_mtu();
    if (mtu == 0) mtu = 679;
    /* Reserve 1 byte for the SBC media payload header (added by send_media) */
    uint32_t max_raw = (g_active_codec == AudioCodec::SBC) ? (mtu - 1) : mtu;

    static thread_local uint8_t accum[2048];
    uint32_t accum_size = 0;
    uint32_t accum_frames = 0;
    uint32_t first_ts = g_timestamp;

    while (offset + bytes_per_encode <= pcm_bytes) {
        uint32_t out_size = static_cast<uint32_t>(g_encode_buffer.size());
        uint32_t out_frames = 0;

        LARGE_INTEGER e0, e1;
        QueryPerformanceCounter(&e0);
        bool ok = encoder->encode(
            pcm_data + offset, bytes_per_encode,
            g_encode_buffer.data(), &out_size, &out_frames
        );
        QueryPerformanceCounter(&e1);
        if (ok && out_size > 0) {
            g_enc_calls++;
            g_enc_bytes_total += out_size;
            g_enc_us_total +=
                (uint64_t)((e1.QuadPart - e0.QuadPart) * 1000000.0 / (double)cb_freq.QuadPart);

            /* Flush if adding this frame would exceed MTU */
            if (accum_size + out_size > max_raw && accum_frames > 0) {
                transport->send_media(
                    accum, accum_size, first_ts,
                    static_cast<uint8_t>(accum_frames), g_active_codec);
                accum_size = 0;
                accum_frames = 0;
                first_ts = g_timestamp;
            }

            if (accum_size + out_size <= sizeof(accum)) {
                memcpy(accum + accum_size, g_encode_buffer.data(), out_size);
                accum_size += out_size;
                accum_frames += out_frames;
            }
        }

        g_timestamp += pcm_frames_per_encode;
        offset += bytes_per_encode;
    }

    /* Flush remaining accumulated frames */
    if (accum_frames > 0) {
        transport->send_media(
            accum, accum_size, first_ts,
            static_cast<uint8_t>(accum_frames), g_active_codec);
    }

    /* Save leftover PCM samples for next callback */
    uint32_t remaining = pcm_bytes - offset;
    if (remaining > 0 && remaining < bytes_per_encode) {
        g_pcm_residual.resize(remaining);
        memcpy(g_pcm_residual.data(), pcm_data + offset, remaining);
    } else {
        g_pcm_residual.clear();
    }

    QueryPerformanceCounter(&cb_end);
    double cb_ms = (double)(cb_end.QuadPart - cb_start.QuadPart) * 1000.0 / (double)cb_freq.QuadPart;
    static double max_cb_ms = 0;
    static DWORD last_cb_diag = 0;
    DWORD cb_now = GetTickCount();
    if (cb_ms > max_cb_ms) max_cb_ms = cb_ms;
    if (cb_now - last_cb_diag >= 2000) {
        last_cb_diag = cb_now;
        fprintf(stderr, "CB: last-call=%.2fms max=%.2fms frames=%u\n",
                cb_ms, max_cb_ms, frames);
        max_cb_ms = 0;
    }

}

/* Console Ctrl+C handler */
static BOOL WINAPI console_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        printf("\nStopping...\n");
        g_running.store(false);
        return TRUE;
    }
    return FALSE;
}

static void print_usage(const char *prog) {
    printf("SSC On Windows v%s (by Salvetum)\n\n", APP_VERSION);
    printf("Usage: %s [options]\n", prog);
    printf("\nModes:\n");
    printf("  (default)    Launch GUI application\n");
    printf("  --cli        Run in command-line mode\n");
    printf("\nOptions (CLI mode):\n");
    printf("  -c <codec>   Codec: ssc, aac, sbc (default: ssc)\n");
    printf("  -q <mode>    Quality mode: hq, sq, mq (default: hq)\n");
    printf("  -d <addr>    Bluetooth device address (XX:XX:XX:XX:XX:XX)\n");
    printf("               If not specified, scans for compatible devices\n");
    printf("  -m <mode>    Capture mode: loopback, virtual (default: loopback)\n");
    printf("  --audio-device <id>  Audio device ID for virtual mode\n");
    printf("  --no-mute-output      Do NOT mute the default speaker while streaming\n");
    printf("  --bit-depth <bits>    PCM bit depth (accepted for compatibility; fixed per codec)\n");
    printf("  --uhq                 SSC UHQ mode: encode at 96 kHz (2x SRC from 48 kHz)\n");
    printf("  --ssc-native          Use the Windows-native Qiling SSC daemon instead of WSL2\n");
    printf("  --bitrate <kbps>      SSC bitrate override (0=auto; snapped to a supported value)\n");
    printf("                        (spawns tools\\ssc_daemon\\sscblobd.py; env SSC_DAEMON_PY override)\n");
    printf("  -l           List available Bluetooth audio devices and exit\n");
    printf("  -u <path>    USB device path for BTstack (optional)\n");
    printf("  -h           Show this help\n");
    printf("\nCodec fallback priority: SSC > AAC > SBC\n");
}

static EncoderQuality parse_quality(const char *mode) {
    if (_stricmp(mode, "hq") == 0) return EncoderQuality::High;
    if (_stricmp(mode, "sq") == 0) return EncoderQuality::Standard;
    if (_stricmp(mode, "mq") == 0) return EncoderQuality::Mobile;
    fprintf(stderr, "Unknown quality mode '%s', using HQ\n", mode);
    return EncoderQuality::High;
}

static const char *codec_name_str(AudioCodec codec) {
    switch (codec) {
    case AudioCodec::SBC: return "SBC";
    case AudioCodec::AAC: return "AAC";
    case AudioCodec::SSC: return "SSC";
    }
    return "Unknown";
}

/*
 * Select best codec from BTstack remote capabilities.
 * Returns true if a compatible codec was found.
 */
static bool find_best_btstack_codec(const BtStackTransport::RemoteCodecCaps &caps,
                                     AudioCodec requested_codec,
                                     AudioCodec *selected_codec) {
    switch (requested_codec) {
    case AudioCodec::SBC: if (caps.sbc) { *selected_codec = AudioCodec::SBC; return true; } break;
    case AudioCodec::AAC: if (caps.aac) { *selected_codec = AudioCodec::AAC; return true; } break;
    case AudioCodec::SSC: if (caps.ssc) { *selected_codec = AudioCodec::SSC; return true; } break;
    }
    printf("Requested codec %s not available, falling back...\n",
           codec_name_str(requested_codec));

    /* Priority: SSC > AAC > SBC */
    if (caps.ssc) { *selected_codec = AudioCodec::SSC; return true; }
    if (caps.aac) { *selected_codec = AudioCodec::AAC; return true; }
    if (caps.sbc) { *selected_codec = AudioCodec::SBC; return true; }

    return false;
}

/* ======================================================================== */
/* Main streaming flow                                                      */
/* ======================================================================== */

static int run_streaming(const uint8_t target_addr[6],
                             const char *usb_path,
                             AudioCodec requested_codec,
                             EncoderQuality quality,
                             CaptureMode capture_mode = CaptureMode::SystemLoopback,
                             const wchar_t *audio_device_id = nullptr,
                             bool mute_output = true,
                             bool uhq = false) {
    BtStackTransport transport;
    transport.set_link_key_dir(get_config_dir());
    transport.set_firmware_dir(get_config_dir());
    transport.set_hci_dump_enabled(true);

    /* Detect embedded Realtek chip from the connected USB adapter so
     * BTstack can load the correct firmware (Windows no longer loads it
     * once the adapter is switched to the WinUSB driver). */
    auto adapters = BtAdapterEnumerator::enumerate();
    for (const auto &a : adapters) {
        if (a.realtek_pid != 0) {
            fprintf(stderr, "BTstack: detected Realtek chip PID=0x%04X\n", a.realtek_pid);
            transport.set_product_id(a.realtek_pid);
            break;
        }
    }

    /* --- Step 2: Initialize BTstack --- */
    printf("\n[2/5] Initializing BTstack (WinUSB transport)...\n");
    if (!transport.init(usb_path)) {
        fprintf(stderr,
            "Failed to initialize BTstack.\n"
            "Ensure a USB Bluetooth adapter is connected and its driver\n"
            "has been replaced with WinUSB using Zadig.\n");
        return 1;
    }

    /* Optional scan: skip if connecting to a known device address */
    printf("\n[2.5/5] Radio ready. Skipping scan (connecting to specified device).\n");

    /* --- Step 3: Connect and discover codecs --- */
    printf("\n[3/5] Connecting and negotiating codec...\n");
    printf("(This may take up to 30 seconds if the device is slow to respond.)\n");
    if (!transport.connect_a2dp(target_addr)) {
        fprintf(stderr,
            "Failed to connect to Bluetooth device.\n"
            "Troubleshooting:\n"
            "  - Ensure the headphones are in PAIRING mode (not just powered on)\n"
            "    (Typically hold the power button until the LED blinks rapidly)\n"
            "  - Ensure no other device is currently connected to the headphones\n"
            "  - Ensure the headphones are within range of the USB adapter\n"
            "  - Try running again -- the first attempt after entering pairing mode may fail\n");
        transport.shutdown();
        return 1;
    }

    /* Select codec */
    AudioCodec selected_codec;
    if (!find_best_btstack_codec(transport.get_remote_caps(),
                                  requested_codec, &selected_codec)) {
        fprintf(stderr, "No compatible codec found on device.\n"
                "Device must support SSC, AAC, or SBC.\n");
        transport.disconnect();
        transport.shutdown();
        return 1;
    }

    g_active_codec = selected_codec;
    printf("Selected codec: %s\n", codec_name_str(selected_codec));

    /* --- Step 4: Initialize audio capture and encoder --- */
    printf("\n[4/5] Initializing audio capture and %s encoder...\n",
           codec_name_str(selected_codec));

    WasapiCapture wasapi_capture;
    std::wstring saved_default_device;

    switch (capture_mode) {
    case CaptureMode::SystemLoopback:
        printf("Capture mode: System Loopback\n");
        if (!wasapi_capture.init()) {
            fprintf(stderr, "Failed to initialize WASAPI capture\n");
            transport.disconnect();
            transport.shutdown();
            return 1;
        }
        break;

    case CaptureMode::VirtualDevice:
        printf("Capture mode: Virtual Device\n");
        if (!audio_device_id) {
            fprintf(stderr, "No audio device ID specified for virtual mode\n");
            transport.disconnect();
            transport.shutdown();
            return 1;
        }
        /* Save and switch default device */
        saved_default_device = AudioDeviceEnumerator::get_default_device_id();
        AudioDeviceEnumerator::set_default_device(audio_device_id);
        if (!wasapi_capture.init(0, audio_device_id)) {
            fprintf(stderr, "Failed to initialize capture on virtual device\n");
            if (!saved_default_device.empty())
                AudioDeviceEnumerator::set_default_device(saved_default_device);
            transport.disconnect();
            transport.shutdown();
            return 1;
        }
        break;
    }

    uint32_t sample_rate = wasapi_capture.get_sample_rate();
    uint32_t channels = wasapi_capture.get_channels();

    /* Determine encode sample rate: for SSC UHQ, encode at 96 kHz even if
     * WASAPI only captures at 48 kHz.  The audio callback applies 2x SRC. */
    uint32_t encode_sr = sample_rate;
    if (selected_codec == AudioCodec::SSC && sample_rate == 48000 && uhq) {
        const auto &rc = transport.get_remote_caps();
        if (rc.ssc_uhq) {
            encode_sr = 96000;
            fprintf(stderr, "\n*** SSC UHQ: encode at 96 kHz (WASAPI capture 48 kHz, 2x SRC) ***\n");
        } else {
            fprintf(stderr,
                    "\n*** SSC UHQ unavailable: remote SSC cap=0x%02X has no UHQ(0x02) bit. ***\n"
                    "*** Falling back to 48 kHz SSC — device does not decode 96 kHz. ***\n",
                    rc.ssc_cap);
        }
    }
    g_encode_sample_rate = encode_sr;

    if (channels > 2) {
        printf("System output has %u channels, downmixing to stereo\n", channels);
    }
    g_active_channels = (channels > 2) ? 2 : channels;

    /* Configure stream — use encode_sr for the remote side */
    if (!transport.configure_codec(selected_codec, encode_sr,
                                    static_cast<uint8_t>(g_active_channels))) {
        fprintf(stderr, "Failed to configure %s stream\n", codec_name_str(selected_codec));
        transport.disconnect();
        transport.shutdown();
        return 1;
    }

    /* Create encoder */
    std::unique_ptr<AudioEncoder> encoder;
    uint16_t media_mtu = transport.get_media_mtu();
    if (media_mtu == 0) media_mtu = 679;

    switch (selected_codec) {
    case AudioCodec::SBC:    encoder = std::make_unique<SbcEncoder>(); break;
#ifdef AAC_ENCODER_AVAILABLE
    case AudioCodec::AAC:    encoder = std::make_unique<AacEncoder>(); break;
#else
    case AudioCodec::AAC:
        fprintf(stderr, "AAC encoder not available (fdk-aac not built)\n");
        transport.disconnect();
        transport.shutdown();
        return 1;
#endif
    case AudioCodec::SSC: {
        auto ssc = std::make_unique<SscEncoder>();
        if (g_ssc_native_daemon) ssc->set_native_daemon(true);
        if (g_ssc_bitrate_kbps > 0) ssc->set_bitrate_override(g_ssc_bitrate_kbps);
        encoder = std::move(ssc);
        break;
    }
    }

    if (!encoder->init(media_mtu, quality, encode_sr, g_active_channels)) {
        fprintf(stderr, "Failed to initialize %s encoder\n", codec_name_str(selected_codec));
        transport.disconnect();
        transport.shutdown();
        return 1;
    }

    /* Set sample width for audio callback: SSC is int32 fixed, SBC/AAC int16. */
    g_encoder_sample_bytes = (selected_codec == AudioCodec::SSC) ? 4 : 2;
    g_pcm_int32_scale = (selected_codec == AudioCodec::SSC) ? 536870912.0 : 2147483647.0;

    /* Start streaming */
    if (!transport.start_stream()) {
        fprintf(stderr, "Failed to start stream\n");
        transport.disconnect();
        transport.shutdown();
        return 1;
    }

    /* --- Step 5: Stream --- */
    printf("\n[5/5] Streaming %s audio (BTstack/WinUSB)...\n", codec_name_str(selected_codec));
    printf("Codec: %s | Bitrate: %u kbps | Sample rate: %u Hz | Channels: %u\n",
           encoder->codec_name(), encoder->get_bitrate_kbps(),
           sample_rate, g_active_channels);
    printf("Press Ctrl+C to stop.\n\n");

    g_encoder.store(encoder.get());
    g_transport.store(&transport);
    g_timestamp = 0;

    /* Audio device survey: find which endpoint actually has signal */
    {
        AudioDeviceEnumerator dev_enum;
        if (dev_enum.init()) {
            auto devices = dev_enum.enumerate();
            std::wstring default_id = AudioDeviceEnumerator::get_default_device_id();
            fprintf(stderr, "--- Audio device survey ---\n");
            for (const auto &d : devices) {
                float peak = AudioDeviceEnumerator::get_device_peak(d.id);
                fprintf(stderr, "Device[%s] '%s' default=%d virtual=%d peak=%.3f dB=%.1f\n",
                        d.id.size() > 40 ? L"..." : d.id.c_str(),
                        d.display_name.c_str(), d.is_default ? 1 : 0,
                        d.is_virtual ? 1 : 0,
                        peak, 20.0 * log10(peak + 1e-12));
                if (!d.is_default && d.display_name == L"") { (void)default_id; }
            }
            if (devices.empty()) {
                fprintf(stderr, "Audio device survey: no render devices found!\n");
            }
        }
    }

    bool cli_capture_started = wasapi_capture.start(audio_callback);
    if (!cli_capture_started) {
        fprintf(stderr, "Failed to start audio capture\n");
        if (!saved_default_device.empty())
            AudioDeviceEnumerator::set_default_device(saved_default_device);
        transport.disconnect();
        transport.shutdown();
        return 1;
    }

    /* Auto-mute the default output so the speakers stay silent while the
     * loopback copy plays through the headphones. Loopback capture reads
     * pre-volume-mix, so muting does not affect the headphones. */
    bool cli_output_muted = false;
    if (mute_output && capture_mode == CaptureMode::SystemLoopback) {
        cli_output_muted = wasapi_capture.mute_output(true);
    }

    /* Main loop: keep streaming, auto-reconnect on disconnect */
    const uint32_t RECONNECT_DELAY_MS = 3000;
    const int MAX_RECONNECT_ATTEMPTS = 10;

    while (g_running.load()) {
        Sleep(200);

        /* Check if connection was lost */
        if (transport.check_disconnected()) {
            printf("\n*** Connection lost — attempting to reconnect ***\n");

            /* Pause audio capture during reconnect to avoid buffering stale data */
            g_transport.store(nullptr);
            g_encoder.store(nullptr);

            bool reconnected = false;
            for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS && g_running.load(); attempt++) {
                printf("Reconnect attempt %d/%d (waiting %u ms)...\n",
                       attempt, MAX_RECONNECT_ATTEMPTS, RECONNECT_DELAY_MS);
                Sleep(RECONNECT_DELAY_MS);

                if (!g_running.load()) break;

                if (transport.reconnect()) {
                    /* Update MTU in case it changed */
                    uint16_t new_mtu = transport.get_media_mtu();
                    if (new_mtu != media_mtu) {
                        printf("Media MTU changed: %u -> %u\n", media_mtu, new_mtu);
                        media_mtu = new_mtu;
                    }
                    reconnected = true;
                    break;
                }
            }

            if (!reconnected) {
                fprintf(stderr, "Failed to reconnect after %d attempts. Exiting.\n",
                        MAX_RECONNECT_ATTEMPTS);
                break;
            }

            /* Resume audio pipeline */
            g_timestamp = 0;
            g_pcm_residual.clear();
            g_encoder.store(encoder.get());
            g_transport.store(&transport);
            printf("*** Reconnected — resuming streaming ***\n\n");
        }
    }

    /* Cleanup */
    printf("\nShutting down...\n");
    wasapi_capture.stop();
    if (cli_output_muted) {
        wasapi_capture.mute_output(false);
        cli_output_muted = false;
    }
    g_encoder.store(nullptr);
    g_transport.store(nullptr);

    transport.stop_stream();
    transport.disconnect();
    transport.shutdown();

    /* Restore default device if we switched it */
    if (!saved_default_device.empty()) {
        AudioDeviceEnumerator::set_default_device(saved_default_device);
    }

    encoder->shutdown();
    printf("Done.\n");
    return 0;
}

/* ======================================================================== */
/* Main                                                                     */
/* ======================================================================== */

int main(int argc, char *argv[]) {
    /* Flush stdout immediately so CLI logs are visible when redirected/pipe */
    setvbuf(stdout, nullptr, _IONBF, 0);

    /* Check for GUI mode (default) vs CLI mode */
    bool cli_mode = false;
    bool start_minimized = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cli") == 0) {
            cli_mode = true;
        } else if (strcmp(argv[i], "--minimized") == 0) {
            start_minimized = true;
        }
    }

    /* Initialize COM: GUI mode needs STA (single-threaded apartment) for
       wxWidgets/OLE message dispatching; CLI mode uses MTA for WASAPI.
       Worker threads initialize their own COM apartments independently. */
    HRESULT hr = CoInitializeEx(nullptr,
        cli_mode ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "Failed to initialize COM: 0x%08lx\n", hr);
        return 1;
    }

    /* Single instance check (GUI mode only) */
    HANDLE single_instance_mutex = nullptr;
    if (!cli_mode) {
        single_instance_mutex = CreateMutexW(nullptr, TRUE, L"SSCOnWindows_SingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            /* Another instance is already running. Try to bring its window to front. */
            wchar_t win_title[64];
            swprintf(win_title, 64, L"SSC On Windows v%hs", APP_VERSION);
            HWND existing = FindWindowW(nullptr, win_title);
            if (existing) {
                if (!IsWindowVisible(existing))
                    ShowWindow(existing, SW_SHOW);
                if (IsIconic(existing))
                    ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
            if (single_instance_mutex) CloseHandle(single_instance_mutex);
            CoUninitialize();
            return 0;
        }
    }

    if (!cli_mode) {
        /* GUI mode (wxWidgets) */
        FreeConsole();
        wxDISABLE_DEBUG_SUPPORT();
        int result = wxEntry(argc, argv);
        if (single_instance_mutex) CloseHandle(single_instance_mutex);
        CoUninitialize();
        return result;
    }

    /* CLI mode */
    printf("SSC On Windows v%s (by Salvetum)\n", APP_VERSION);
    printf("Codecs: SSC | AAC | SBC\n");
    printf("===================================================\n\n");

    /* Parse command-line arguments */
    EncoderQuality quality = EncoderQuality::High;
    AudioCodec requested_codec = AudioCodec::SSC;
    char device_addr_str[32] = {};
    bool list_only = false;
    const char *usb_path = nullptr;
    CaptureMode cli_capture_mode = CaptureMode::SystemLoopback;
    const char *cli_audio_device = nullptr;
    bool cli_mute_output = true;
    bool cli_uhq = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cli") == 0) {
            continue;  /* Already handled */
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            i++;
            if (_stricmp(argv[i], "sbc") == 0) {
                requested_codec = AudioCodec::SBC;
            } else if (_stricmp(argv[i], "aac") == 0) {
                requested_codec = AudioCodec::AAC;
            } else if (_stricmp(argv[i], "ssc") == 0 || _stricmp(argv[i], "auto") == 0) {
                requested_codec = AudioCodec::SSC;
            } else {
                fprintf(stderr, "Unknown codec '%s'. Use: ssc, aac, sbc\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            quality = parse_quality(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            strncpy(device_addr_str, argv[++i], sizeof(device_addr_str) - 1);
        } else if (strcmp(argv[i], "-a") == 0) {
            /* Deprecated: LDAC ABR removed. Ignored for compatibility. */
        } else if (strcmp(argv[i], "-l") == 0) {
            list_only = true;
        } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            usb_path = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            i++;
            if (_stricmp(argv[i], "loopback") == 0) {
                cli_capture_mode = CaptureMode::SystemLoopback;
            } else if (_stricmp(argv[i], "virtual") == 0) {
                cli_capture_mode = CaptureMode::VirtualDevice;
            } else {
                fprintf(stderr, "Unknown capture mode '%s'. Use: loopback, virtual\n",
                        argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--audio-device") == 0 && i + 1 < argc) {
            cli_audio_device = argv[++i];
        } else if (strcmp(argv[i], "--no-mute-output") == 0) {
            cli_mute_output = false;
        } else if (strcmp(argv[i], "--bit-depth") == 0 && i + 1 < argc) {
            ++i; /* accepted for compatibility; bit depth is fixed per codec */
        } else if (strcmp(argv[i], "--uhq") == 0) {
            cli_uhq = true;
        } else if (strcmp(argv[i], "--ssc-native") == 0) {
            g_ssc_native_daemon = true;
        } else if (strcmp(argv[i], "--bitrate") == 0 && i + 1 < argc) {
            g_ssc_bitrate_kbps = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    printf("Transport: BTstack (WinUSB)\n");

    SetConsoleCtrlHandler(console_handler, TRUE);

    /* --- Step 1: Discover/select Bluetooth device --- */
    printf("[1/5] Scanning for Bluetooth audio devices...\n");

    /*
     * Windows BT APIs may not work if the adapter is claimed by WinUSB.
     * Try auto-discovery, but device address can be specified via -d.
     */
    if (list_only) {
        auto bt_devices = BtDeviceDiscovery::scan_paired_devices();
        printf("\nDone.\n");
        CoUninitialize();
        return 0;
    }

    if (device_addr_str[0] == '\0') {
        /* Try Windows discovery (works if a separate adapter is available) */
        auto bt_devices = BtDeviceDiscovery::scan_paired_devices();
        for (const auto &dev : bt_devices) {
            if (dev.a2dp_sink) {
                auto addr_str = BtDeviceDiscovery::format_address(dev.address);
                strncpy(device_addr_str, addr_str.c_str(), sizeof(device_addr_str) - 1);
                printf("Auto-selected device: %s (%s)\n", dev.name.c_str(), device_addr_str);
                break;
            }
        }
        if (device_addr_str[0] == '\0') {
            fprintf(stderr,
                "No Bluetooth audio device found.\n"
                "Use -d XX:XX:XX:XX:XX:XX to specify the device address.\n"
                "(Find the address in Windows Settings > Bluetooth before switching to WinUSB)\n");
            CoUninitialize();
            return 1;
        }
    }

    uint8_t target_addr[6] = {};
    if (device_addr_str[0] != '\0') {
        if (!BtDeviceDiscovery::parse_address(device_addr_str, target_addr)) {
            fprintf(stderr, "Invalid Bluetooth address: %s\n", device_addr_str);
            CoUninitialize();
            return 1;
        }
        printf("Target device: %s\n",
               BtDeviceDiscovery::format_address(target_addr).c_str());
    } else {
        fprintf(stderr, "No Bluetooth audio device found. Use -d to specify an address.\n");
        CoUninitialize();
        return 1;
    }

    /* Convert audio device ID to wide string if specified */
    std::wstring cli_audio_device_w;
    if (cli_audio_device) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cli_audio_device, -1, nullptr, 0);
        if (wlen > 0) {
            cli_audio_device_w.resize(wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, cli_audio_device, -1,
                                &cli_audio_device_w[0], wlen);
        }
    }

    int result = run_streaming(target_addr, usb_path,
                               requested_codec, quality,
                               cli_capture_mode,
                               cli_audio_device_w.empty() ? nullptr : cli_audio_device_w.c_str(),
                               cli_mute_output, cli_uhq);

    CoUninitialize();
    return result;
}
