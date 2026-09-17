/*
 * sscblobd - persistent SSC encode daemon for WSL2.
 *
 * Spawns qemu-aarch64 running ssc_blob_helper (Samsung libScalable_Encoder.so)
 * and serves encode requests over TCP so the Windows A2DP bridge can encode
 * PCM -> SSC in near real-time without spawning a process per frame.
 *
 * Protocol (all little-endian, one connection at a time):
 *   client -> server: uint32 frame_samples, then frame_samples*channels*2 bytes int16 PCM
 *   server -> client: int32 ret, then ret bytes of encoded SSC frame
 *
 * The int16 PCM is converted to int32 (<<14) exactly like openssc's
 * sscenc_encode_s16 does before handing it to the blob helper.
 *
 * Usage: sscblobd <port> <sample-rate> <channels> <bitrate> [helper-path]
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAX_FRAME_SAMPLES
#define MAX_FRAME_SAMPLES 16384u
#endif
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 2u
#endif
#ifndef MAX_ENCODE_BYTES
#define MAX_ENCODE_BYTES 65536u
#endif

#define DEFAULT_HELPER "/home/kaan5/ssc/openssc/build_blob/ssc_blob_helper"
#define DEFAULT_BLOB "/home/kaan5/ssc/blob/libScalable_Encoder.so"
#define SYSROOT "/usr/aarch64-linux-gnu"
#define SHIMDIR "/home/kaan5/ssc/openssc/build_blob"

static int g_stop = 0;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static int read_full(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r == 0)
            return -1;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

static pid_t spawn_helper(const char *helper_path, int sample_rate, int channels,
                          int bitrate, int in_fds[2], int out_fds[2])
{
    char rate_s[16], ch_s[8], br_s[16];
    const char *bits_s = "24";

    snprintf(rate_s, sizeof(rate_s), "%d", sample_rate);
    snprintf(ch_s, sizeof(ch_s), "%d", channels);
    snprintf(br_s, sizeof(br_s), "%d", bitrate);

    if (pipe(in_fds) < 0 || pipe(out_fds) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(in_fds[0]);
        close(in_fds[1]);
        close(out_fds[0]);
        close(out_fds[1]);
        return -1;
    }
    if (pid == 0) {
        (void)dup2(in_fds[0], STDIN_FILENO);
        (void)dup2(out_fds[1], STDOUT_FILENO);
        close(in_fds[0]);
        close(in_fds[1]);
        close(out_fds[0]);
        close(out_fds[1]);
        char *env_str;
        if (asprintf(&env_str, "LD_LIBRARY_PATH=%s", SHIMDIR) < 0)
            _exit(127);
        execl("/usr/bin/qemu-aarch64", "qemu-aarch64", "-L", SYSROOT,
              "-E", env_str, helper_path, DEFAULT_BLOB,
              rate_s, ch_s, bits_s, br_s, (char *)NULL);
        _exit(127);
    }
    close(in_fds[0]);
    close(out_fds[1]);
    return pid;
}

static int service_client(int client_fd, const char *helper_path, int sample_rate,
                          int channels, int bitrate)
{
    int in_fds[2] = {-1, -1}, out_fds[2] = {-1, -1};
    pid_t pid = spawn_helper(helper_path, sample_rate, channels, bitrate,
                             in_fds, out_fds);
    if (pid < 0) {
        fprintf(stderr, "sscblobd: spawn_helper failed\n");
        return -1;
    }

    int32_t ready = -1;
    if (read_full(out_fds[0], &ready, sizeof(ready)) < 0 || ready != 0) {
        fprintf(stderr, "sscblobd: helper not ready (ready=%d)\n", ready);
        close(in_fds[1]);
        close(out_fds[0]);
        (void)kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        return -1;
    }
    fprintf(stderr, "sscblobd: helper ready (rate=%d ch=%d br=%d)\n",
            sample_rate, channels, bitrate);

    int32_t *pcm32 = malloc(MAX_FRAME_SAMPLES * MAX_CHANNELS * sizeof(*pcm32));
    uint8_t *out = malloc(MAX_ENCODE_BYTES);
    if (!pcm32 || !out) {
        fprintf(stderr, "sscblobd: alloc failed\n");
        goto done;
    }

    for (;;) {
        uint32_t frame_samples = 0;
        if (read_full(client_fd, &frame_samples, sizeof(frame_samples)) < 0)
            break; /* client closed */
        if (frame_samples == 0 || frame_samples > MAX_FRAME_SAMPLES) {
            fprintf(stderr, "sscblobd: bad frame_samples=%u\n", frame_samples);
            break;
        }
        /* int32 PCM (symmetrical to helper): no 16-bit conversion needed */
        size_t pcm32_bytes = (size_t)frame_samples * (size_t)channels * sizeof(int32_t);
        if (read_full(client_fd, pcm32, pcm32_bytes) < 0)
            break;

        if (write_full(in_fds[1], &frame_samples, sizeof(frame_samples)) < 0 ||
            write_full(in_fds[1], pcm32, (size_t)frame_samples * channels * sizeof(int32_t)) < 0) {
            fprintf(stderr, "sscblobd: helper pipe write failed\n");
            break;
        }

        int32_t ret = -1;
        if (read_full(out_fds[0], &ret, sizeof(ret)) < 0) {
            fprintf(stderr, "sscblobd: helper pipe read failed\n");
            break;
        }
        if (ret > (int32_t)MAX_ENCODE_BYTES)
            ret = 0;
        if (ret > 0 && read_full(out_fds[0], out, (size_t)ret) < 0) {
            fprintf(stderr, "sscblobd: helper frame read failed\n");
            break;
        }

        if (write_full(client_fd, &ret, sizeof(ret)) < 0)
            break;
        if (ret > 0 && write_full(client_fd, out, (size_t)ret) < 0)
            break;
    }

done:
    free(pcm32);
    free(out);
    close(in_fds[1]);
    close(out_fds[0]);
    (void)kill(pid, SIGKILL);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <port> <sample-rate> <channels> <bitrate> [helper-path]\n",
                argv[0]);
        return 2;
    }
    int port = atoi(argv[1]);
    int sample_rate = atoi(argv[2]);
    int channels = atoi(argv[3]);
    int bitrate = atoi(argv[4]);
    const char *helper_path = argc >= 6 ? argv[5] : DEFAULT_HELPER;

    if (port <= 0 || sample_rate <= 0 || channels <= 0 || channels > MAX_CHANNELS || bitrate <= 0) {
        fprintf(stderr, "sscblobd: bad args\n");
        return 2;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: let accept() return EINTR so g_stop wins */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGCHLD, SIG_IGN);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    (void)setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(sfd, 4) < 0) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "sscblobd: listening on 0.0.0.0:%d (rate=%d ch=%d br=%d helper=%s)\n",
            port, sample_rate, channels, bitrate, helper_path);

    while (!g_stop) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        fprintf(stderr, "sscblobd: client connected\n");
        int one2 = 1;
        (void)setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one2, sizeof(one2));
        (void)service_client(cfd, helper_path, sample_rate, channels, bitrate);
        close(cfd);
        fprintf(stderr, "sscblobd: client disconnected\n");
    }
    close(sfd);
    return 0;
}