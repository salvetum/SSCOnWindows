/*
 * SSC Encoder Wrapper
 *
 * SPDX-License-Identifier: MIT
 */

#include "ssc_encoder.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <ws2tcpip.h>
#include <winsock2.h>
#endif

namespace {

constexpr int kDaemonPort = 20248;
constexpr const char *kDaemonScript =
    "/home/kaan5/ssc/bin/start_sscblobd";
constexpr uint32_t kFrameSamples = 864;
constexpr uint32_t kMaxEncodeBytes = 4096;
constexpr uint64_t kRecoveryCooldownMs = 5000;

/* SSC bitrates are gated by the A2DP capability mode (see openssc
 * pipewire/a2dp-codec-ssc.c). At 48 kHz we advertise SSC_CAP_BASIC_48K (0x0C),
 * which only permits the "basic" set below; the UHQ bitrates (152/250/291/308/
 * 442/584/886) require the UHQ2 (0x02) capability bit, which the Buds3 FE does
 * not advertise (remote cap=0x3C). Feeding any other value to the blob makes it
 * emit a malformed frame -> garbled audio, so requests are snapped here.
 * Keep each list sorted ascending. */
const uint32_t kBasicBitratesBps[] = {
    88000, 96000, 128000, 192000, 229000, 256000, 328000,
};
const uint32_t kUhqBitratesBps[] = {
    152000, 250000, 291000, 308000, 442000, 584000, 886000,
};

uint32_t snap_bitrate_bps(uint32_t bps, uint32_t sample_rate) {
    bool uhq = (sample_rate == 88200 || sample_rate == 96000);
    const uint32_t *list = uhq ? kUhqBitratesBps : kBasicBitratesBps;
    const size_t n = uhq ? (sizeof(kUhqBitratesBps) / sizeof(kUhqBitratesBps[0]))
                         : (sizeof(kBasicBitratesBps) / sizeof(kBasicBitratesBps[0]));
    uint32_t best = list[0];
    uint32_t best_diff = (bps > best) ? (bps - best) : (best - bps);
    for (size_t i = 1; i < n; ++i) {
        uint32_t cand = list[i];
        uint32_t diff = (bps > cand) ? (bps - cand) : (cand - bps);
        if (diff < best_diff) {
            best_diff = diff;
            best = cand;
        }
    }
    return best;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

SscEncoder::SscEncoder() = default;

SscEncoder::~SscEncoder() {
    shutdown();
}

uint32_t SscEncoder::pick_bitrate(EncoderQuality quality, uint32_t sample_rate) const {
    bool uhq = (sample_rate == 88200 || sample_rate == 96000);
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

uint32_t SscEncoder::snap_bitrate_kbps(uint32_t kbps, uint32_t sample_rate) {
    if (kbps == 0) return 0;            /* 0 = auto, leave untouched */
    return snap_bitrate_bps(kbps * 1000, sample_rate) / 1000;
}

std::string SscEncoder::run_wsl(const std::string &args) {
    std::string out;
#ifdef _WIN32
    std::string cmdline = "-d Ubuntu -- " + args;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";
    if (!SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    char cmdline_buf[1024];
    snprintf(cmdline_buf, sizeof(cmdline_buf), "wsl.exe %s", cmdline.c_str());

    BOOL ok = CreateProcessA(nullptr, cmdline_buf, nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return "";
    }

    char buf[4096];
    DWORD nread = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &nread, nullptr) && nread > 0) {
        out.append(buf, nread);
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#endif
    return out;
}

bool SscEncoder::start_daemon(uint32_t sample_rate, uint32_t channels, uint32_t bitrate) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    /* Drop any stale socket / native daemon from a previous run. */
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    std::string ip;
    if (native_daemon_) {
        /* Windows-native Qiling daemon: spawn sscblobd.py, connect to loopback. */
        if (!start_native_daemon()) {
            fprintf(stderr, "SSC: native daemon spawn failed\n");
            return false;
        }
        ip = "127.0.0.1";
    } else {
        /* Restart the WSL2 daemon with our parameters (kills any prior instance). */
        char daemon_cmd[256];
        snprintf(daemon_cmd, sizeof(daemon_cmd), "bash -lc '%s %d %u %u %u'",
                 kDaemonScript, kDaemonPort, sample_rate, channels, bitrate);
        run_wsl(daemon_cmd);

        /* Grab the WSL2 IP */
        ip = trim(run_wsl("bash -lc 'hostname -I'"));
        if (ip.empty()) {
            fprintf(stderr, "SSC: could not resolve WSL2 IP\n");
            return false;
        }
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)kDaemonPort);
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        if (connect(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
            int one = 1;
            setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char *>(&one), sizeof(one));
            DWORD timeout = 5000;
            setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char *>(&timeout), sizeof(timeout));
            setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO,
                       reinterpret_cast<const char *>(&timeout), sizeof(timeout));
            connected_ = true;
            return true;
        }
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    fprintf(stderr, "SSC: could not connect to daemon at %s:%d\n", ip.c_str(), kDaemonPort);
    return false;
#else
    (void)sample_rate; (void)channels; (void)bitrate;
    return false;
#endif
}

bool SscEncoder::start_native_daemon() {
#ifdef _WIN32
    HANDLE previous = reinterpret_cast<HANDLE>(native_proc_);
    if (previous) {
        TerminateProcess(previous, 1);
        CloseHandle(previous);
        native_proc_ = nullptr;
    }

    std::string script = find_native_daemon_script();
    if (script.empty()) {
        fprintf(stderr, "SSC: no native daemon script found\n");
        return false;
    }

    std::string cmdline = "py -3.14 -u \"" + script + "\" " +
        std::to_string(kDaemonPort) + " " +
        std::to_string(sample_rate_) + " " +
        std::to_string(channels_) + " " +
        std::to_string(bitrate_);

    /* CREATE_NO_WINDOW + STARTF_USESHOWWINDOW/SW_HIDE: no console pops up. */
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        fprintf(stderr, "SSC: CreateProcess failed for native daemon (err=%lu)\n",
                GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    native_proc_ = pi.hProcess;
    fprintf(stderr, "SSC: native daemon spawned (pid=%lu, rate=%u ch=%u br=%u)\n",
            pi.dwProcessId, sample_rate_, channels_, bitrate_);
    return true;
#else
    return false;
#endif
}

std::string SscEncoder::find_native_daemon_script() {
#ifdef _WIN32
    /* 1) explicit override */
    const char *env = getenv("SSC_DAEMON_PY");
    if (env && *env) return std::string(env);

    /* 2) next to the running exe (deployed layout) */
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, sizeof(exe));
    std::string dir = exe;
    size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) {
        std::string candidate = dir.substr(0, slash + 1) + "sscblobd.py";
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
        candidate = dir.substr(0, slash + 1) + "..\\tools\\ssc_daemon\\sscblobd.py";
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }

    /* 3) repo layout (dev tree) */
    std::string repo = "C:\\Projects\\SSCOnWindows\\tools\\ssc_daemon\\sscblobd.py";
    if (GetFileAttributesA(repo.c_str()) != INVALID_FILE_ATTRIBUTES)
        return repo;
#endif
    return {};
}

bool SscEncoder::recover() {
#ifdef _WIN32
    uint64_t now = GetTickCount64();
    if (now < next_recovery_ms_) {
        return connected_;   /* cooldown: back off before hammering wsl.exe */
    }
    next_recovery_ms_ = now + kRecoveryCooldownMs;

    connected_ = false;
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    fprintf(stderr, "SSC: daemon lost, restarting (recovery #%u)...\n",
            recovery_count_ + 1);
    bool ok = start_daemon(sample_rate_, channels_, bitrate_);
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
        br = snap_bitrate_bps(req, rate);
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
    sample_rate_ = sample_rate;
    bitrate_ = br;

    if (!start_daemon(sample_rate, channels_, br)) {
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

    if (!connected_ || sock_ == INVALID_SOCKET) {
        if (!recover()) return false;
    }

    uint32_t expect_bytes = kFrameSamples * channels_ * sizeof(int32_t);
    if (pcm_bytes < expect_bytes) {
        return true;            /* not enough data yet, not an error */
    }

#ifdef _WIN32
    LARGE_INTEGER sq, eq, sfq, hq, fq, frq;
    QueryPerformanceFrequency(&frq);
    QueryPerformanceCounter(&sq);

    /* frame_samples LE + 32-bit interleaved PCM (24-bit, 2^29 scale) */
    uint32_t frame_samples = kFrameSamples;
    int r;
    r = send(sock_, reinterpret_cast<const char *>(&frame_samples), 4, 0);
    if (r != 4) { connected_ = false; recover(); return false; }
    r = send(sock_, reinterpret_cast<const char *>(pcm_data), expect_bytes, 0);
    if (r != static_cast<int>(expect_bytes)) { connected_ = false; recover(); return false; }
    QueryPerformanceCounter(&eq);

    int32_t ret = 0;
    size_t got = 0;
    while (got < 4) {
        int n = recv(sock_, reinterpret_cast<char *>(&ret) + got, 4 - got, 0);
        if (n <= 0) { connected_ = false; recover(); return false; }
        got += static_cast<size_t>(n);
    }
    QueryPerformanceCounter(&sfq);
    if (ret <= 0 || ret > static_cast<int32_t>(kMaxEncodeBytes)) return false;
    got = 0;
    while (got < static_cast<size_t>(ret)) {
        int n = recv(sock_, reinterpret_cast<char *>(out_data) + got, ret - got, 0);
        if (n <= 0) { connected_ = false; recover(); return false; }
        got += static_cast<size_t>(n);
    }
    QueryPerformanceCounter(&fq);

    static double max_total = 0.0;
    static uint32_t diag_count = 0;
    static DWORD diag_tick = 0;
    diag_count++;
    double t_send = (double)(eq.QuadPart - sq.QuadPart) * 1000.0 / (double)frq.QuadPart;
    double t_hdr  = (double)(sfq.QuadPart - eq.QuadPart) * 1000.0 / (double)frq.QuadPart;
    double t_data = (double)(fq.QuadPart - sfq.QuadPart) * 1000.0 / (double)frq.QuadPart;
    double t_tot  = (double)(fq.QuadPart - sq.QuadPart) * 1000.0 / (double)frq.QuadPart;
    if (t_tot > max_total) max_total = t_tot;
    if (diag_tick == 0) diag_tick = GetTickCount();
    if (GetTickCount() - diag_tick >= 2000) {
        fprintf(stderr, "SSC: enc%u t_send=%.2fms t_hdr=%.2fms t_data=%.2fms total=%.2fms max=%.2fms ret=%d\n",
                diag_count, t_send, t_hdr, t_data, t_tot, max_total, ret);
        diag_count = 0; diag_tick = GetTickCount(); max_total = 0.0;
    }
#else
    (void)pcm_data; (void)out_data;
    int ret = 0;
#endif

    *out_size = static_cast<uint32_t>(ret);
    if (out_frames) *out_frames = 1;
    return true;
}

void SscEncoder::shutdown() {
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    if (native_proc_) {
        HANDLE h = reinterpret_cast<HANDLE>(native_proc_);
        TerminateProcess(h, 1);
        CloseHandle(h);
        native_proc_ = nullptr;
    }
    WSACleanup();
#endif
    connected_ = false;
    initialized_ = false;
}