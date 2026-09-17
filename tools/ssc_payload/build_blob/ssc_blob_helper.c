#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FRAME_SAMPLES 8192u
#define MAX_CHANNELS 2u
#define MAX_ENCODE_BYTES 4096u

typedef int (*ssc_encoder_get_size_fn)(int channels, int sample_rate);
typedef int (*ssc_encoder_init_fn)(void *state, int sample_rate, int channels, int bit_depth);
typedef void *(*ssc_encoder_create_fn)(int sample_rate, int channels, int bit_depth, int *err);
typedef int (*ssc_encode_fn)(void *state, const int32_t *pcm, int frame_samples, uint8_t *out, int bitrate);

static int read_full(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r == 0)
            return 0;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 1;
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

static long parse_long(const char *s)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || !end || *end) {
        fprintf(stderr, "invalid integer argument: %s\n", s);
        exit(2);
    }
    return v;
}

static void *must_sym(void *handle, const char *name)
{
    void *sym = dlsym(handle, name);
    if (!sym) {
        fprintf(stderr, "missing blob symbol %s: %s\n", name, dlerror());
        exit(3);
    }
    return sym;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s libScalable_Encoder.so sample-rate channels bit-depth bitrate\n", argv[0]);
        return 2;
    }

    const char *so_path = argv[1];
    int sample_rate = (int)parse_long(argv[2]);
    int channels = (int)parse_long(argv[3]);
    int bit_depth = (int)parse_long(argv[4]);
    int bitrate = (int)parse_long(argv[5]);

    if (channels <= 0 || channels > (int)MAX_CHANNELS || sample_rate <= 0 || bit_depth <= 0 || bitrate <= 0) {
        fprintf(stderr, "invalid encoder config rate=%d channels=%d bits=%d bitrate=%d\n",
                sample_rate, channels, bit_depth, bitrate);
        return 2;
    }

    void *lib = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "dlopen(%s) failed: %s\n", so_path, dlerror());
        return 3;
    }

    ssc_encoder_get_size_fn get_size = (ssc_encoder_get_size_fn)must_sym(lib, "ssc_encoder_get_size");
    ssc_encoder_init_fn init = (ssc_encoder_init_fn)must_sym(lib, "ssc_encoder_init");
    ssc_encoder_create_fn create = (ssc_encoder_create_fn)must_sym(lib, "ssc_encoder_create");
    ssc_encode_fn encode = (ssc_encode_fn)must_sym(lib, "ssc_encode");

    int err = -1;
    int warm_size = get_size(channels, sample_rate);
    if (warm_size <= 0 || warm_size > 16 * 1024 * 1024) {
        fprintf(stderr, "ssc_encoder_get_size(%d,%d) returned %d\n", channels, sample_rate, warm_size);
        return 4;
    }
    void *warm_state = calloc(1, (size_t)warm_size);
    if (!warm_state) {
        fprintf(stderr, "warmup allocation failed\n");
        return 5;
    }
    int init_ret = init(warm_state, sample_rate, channels, bit_depth);
    if (init_ret != 0) {
        fprintf(stderr, "ssc_encoder_init(%d,%d,%d) returned %d\n", sample_rate, channels, bit_depth, init_ret);
        return 6;
    }

    void *state = create(sample_rate, channels, bit_depth, &err);
    if (!state || err != 0) {
        fprintf(stderr, "ssc_encoder_create(%d,%d,%d) failed err=%d state=%p\n",
                sample_rate, channels, bit_depth, err, state);
        return 4;
    }

    int32_t *pcm = calloc(MAX_FRAME_SAMPLES * MAX_CHANNELS, sizeof(*pcm));
    uint8_t *out = calloc(MAX_ENCODE_BYTES, 1);
    if (!pcm || !out) {
        fprintf(stderr, "allocation failed\n");
        return 5;
    }

    int32_t ready = 0;
    if (write_full(STDOUT_FILENO, &ready, sizeof(ready)) < 0)
        return 9;

    for (;;) {
        uint32_t frame_samples = 0;
        int rr = read_full(STDIN_FILENO, &frame_samples, sizeof(frame_samples));
        if (rr == 0)
            break;
        if (rr < 0) {
            perror("read frame_samples");
            return 6;
        }
        if (frame_samples == 0 || frame_samples > MAX_FRAME_SAMPLES) {
            fprintf(stderr, "invalid frame_samples=%u\n", frame_samples);
            return 7;
        }

        size_t pcm_bytes = (size_t)frame_samples * (size_t)channels * sizeof(*pcm);
        rr = read_full(STDIN_FILENO, pcm, pcm_bytes);
        if (rr <= 0) {
            fprintf(stderr, "short pcm read\n");
            return 8;
        }

        memset(out, 0, MAX_ENCODE_BYTES);
        int ret = encode(state, pcm, (int)frame_samples, out, bitrate);
        if (write_full(STDOUT_FILENO, &ret, sizeof(ret)) < 0)
            return 9;
        if (ret > 0) {
            if (ret > (int)MAX_ENCODE_BYTES) {
                fprintf(stderr, "ssc_encode returned oversized frame %d\n", ret);
                return 10;
            }
            if (write_full(STDOUT_FILENO, out, (size_t)ret) < 0)
                return 9;
        }
    }

    free(out);
    free(pcm);
    return 0;
}
