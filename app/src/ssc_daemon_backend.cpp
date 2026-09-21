/*
 * Daemon SSC Encode Backend
 *
 * SPDX-License-Identifier: MIT
 */

#include "ssc_daemon_backend.h"
#include "console_style.h"

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
/* Wire-protocol magic (LE bytes 'S''H''C''D') the client sends as the
 * frame_samples header to make the daemon exit cleanly. */
constexpr uint32_t kCmdShutdown = 0x44434853u;

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

DaemonSscBackend::DaemonSscBackend(bool native) : native_(native) {}

DaemonSscBackend::~DaemonSscBackend() {
    shutdown();
}

std::string DaemonSscBackend::run_wsl(const std::string &args) {
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

bool DaemonSscBackend::start_daemon() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    /* Drop any stale socket / native daemon from a previous run. */
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    std::string ip;
    if (native_) {
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
                 kDaemonScript, kDaemonPort, sample_rate_, channels_, bitrate_bps_);
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
    return false;
#endif
}

bool DaemonSscBackend::start_native_daemon() {
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
        std::to_string(bitrate_bps_);

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
            pi.dwProcessId, sample_rate_, channels_, bitrate_bps_);
    return true;
#else
    return false;
#endif
}

std::string DaemonSscBackend::find_native_daemon_script() {
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

bool DaemonSscBackend::init(uint32_t sample_rate, uint32_t channels,
                            uint32_t bitrate_bps) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    bitrate_bps_ = bitrate_bps;
    return start_daemon();
}

bool DaemonSscBackend::recover() {
#ifdef _WIN32
    connected_ = false;
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    fprintf(stderr, "SSC: daemon lost, restarting\n");
    return start_daemon();
#else
    return false;
#endif
}

bool DaemonSscBackend::encode(const int32_t *pcm, size_t frames, size_t channels,
                              uint8_t *out, size_t out_cap, size_t *out_size) {
    if (!out_size) return false;
    *out_size = 0;
    if (!connected() || !pcm) return false;

    uint32_t expect_bytes = static_cast<uint32_t>(frames) * static_cast<uint32_t>(channels)
                            * sizeof(int32_t);
    if (expect_bytes > out_cap) return false;

#ifdef _WIN32
    LARGE_INTEGER sq, eq, sfq, hq, fq, frq;
    QueryPerformanceFrequency(&frq);
    QueryPerformanceCounter(&sq);

    /* frame_samples LE + 32-bit interleaved PCM (24-bit, 2^29 scale) */
    uint32_t frame_samples = static_cast<uint32_t>(frames);
    int r;
    r = send(sock_, reinterpret_cast<const char *>(&frame_samples), 4, 0);
    if (r != 4) return false;
    r = send(sock_, reinterpret_cast<const char *>(pcm), expect_bytes, 0);
    if (r != static_cast<int>(expect_bytes)) return false;
    QueryPerformanceCounter(&eq);

    int32_t ret = 0;
    size_t got = 0;
    while (got < 4) {
        int n = recv(sock_, reinterpret_cast<char *>(&ret) + got, 4 - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    QueryPerformanceCounter(&sfq);
    if (ret <= 0 || ret > static_cast<int32_t>(kMaxEncodeBytes)) return false;
    if (static_cast<size_t>(ret) > out_cap) return false;
    got = 0;
    while (got < static_cast<size_t>(ret)) {
        int n = recv(sock_, reinterpret_cast<char *>(out) + got, ret - got, 0);
        if (n <= 0) return false;
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
        cstyle::fprint(stderr, cstyle::Tag::Data, "SSC:     ");
        fprintf(stderr, "enc%u t_send=%.2fms t_hdr=%.2fms t_data=%.2fms total=",
                diag_count, t_send, t_hdr, t_data);
        if (t_tot > 15.0) cstyle::fprint(stderr, cstyle::Tag::Warn, "%.2fms", t_tot);
        else fprintf(stderr, "%.2fms", t_tot);
        fprintf(stderr, " max=%.2fms ret=", max_total);
        if (ret <= 0) cstyle::fprint(stderr, cstyle::Tag::Error, "%d", ret);
        else fprintf(stderr, "%d", ret);
        fprintf(stderr, "\n");
        diag_count = 0; diag_tick = GetTickCount(); max_total = 0.0;
    }

    *out_size = static_cast<size_t>(ret);
    return true;
#else
    (void)pcm; (void)frames; (void)channels; (void)out; (void)out_cap;
    return false;
#endif
}

void DaemonSscBackend::request_shutdown() {
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET && connected_) {
        uint32_t magic = kCmdShutdown;
        (void)send(sock_, reinterpret_cast<const char *>(&magic), 4, 0);
    }
#endif
}

void DaemonSscBackend::shutdown() {
#ifdef _WIN32
    request_shutdown();
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
}