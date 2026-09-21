/*
 * Daemon SSC Encode Backend
 *
 * The current (and default) SscEncodeBackend: the real Samsung
 * libScalable_Encoder.so blob executed on an aarch64 emulator behind a TCP
 * daemon (WSL2/qemu by default, or the Windows-native Qiling daemon with
 * --ssc-native). This class owns the daemon lifecycle, the socket, and the
 * wire protocol.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SSC_DAEMON_BACKEND_H
#define SSC_DAEMON_BACKEND_H

#include "ssc_encode_backend.h"

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#endif

class DaemonSscBackend : public SscEncodeBackend {
public:
    /* native: use the Qiling daemon (sscblobd.py) instead of WSL2/qemu. */
    explicit DaemonSscBackend(bool native);
    ~DaemonSscBackend() override;

    bool init(uint32_t sample_rate, uint32_t channels,
              uint32_t bitrate_bps) override;
    bool encode(const int32_t *pcm, size_t frames, size_t channels,
                uint8_t *out, size_t out_cap, size_t *out_size) override;
    bool recover() override;
    void shutdown() override;
    bool connected() const override { return connected_ && sock_ != INVALID_SOCKET; }

private:
    /* Send the wire-protocol CMD_SHUTDOWN magic so the daemon exits cleanly
     * instead of the client just dropping the socket. Best effort. */
    void request_shutdown();

    /* (Re)start the daemon and connect to it for the configured stream. */
    bool start_daemon();

    /* Spawn the Windows-native Qiling daemon (sscblobd.py). */
    bool start_native_daemon();

    /* Locate the native daemon script (env SSC_DAEMON_PY, exe-relative, repo). */
    std::string find_native_daemon_script();

    /* Run a wsl.exe command, capture stdout. */
    std::string run_wsl(const std::string &args);

    bool native_ = false;
    bool connected_ = false;
    uint32_t sample_rate_ = 48000;
    uint32_t channels_ = 2;
    uint32_t bitrate_bps_ = 229000;

#ifdef _WIN32
    SOCKET sock_ = INVALID_SOCKET;
    void *native_proc_ = nullptr;   /* HANDLE to sscblobd.py process */
#endif
};

#endif /* SSC_DAEMON_BACKEND_H */