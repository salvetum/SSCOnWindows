typedef __SIZE_TYPE__ size_t;

enum { PROT_READ = 1, PROT_WRITE = 2, MAP_PRIVATE = 2, MAP_ANONYMOUS = 0x20 };

static long raw_syscall3(long n, long a0, long a1, long a2)
{
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x8 asm("x8") = n;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static long raw_syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x8 asm("x8") = n;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
}

static void shim_write(const char *s, size_t n)
{
    (void)raw_syscall3(64, 2, (long)s, (long)n);
}

__attribute__((visibility("default"))) void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}

__attribute__((visibility("default"))) void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i)
        d[i] = (unsigned char)value;
    return dst;
}

static unsigned char *heap_cur;
static unsigned char *heap_end;

__attribute__((visibility("default"))) void *malloc(size_t n)
{
    n = (n + 15u) & ~(size_t)15u;
    if (n == 0)
        n = 16;
    if (!heap_cur || (size_t)(heap_end - heap_cur) < n) {
        size_t request = n < (1u << 20) ? (1u << 20) : ((n + 4095u) & ~(size_t)4095u);
        long p = raw_syscall6(222, 0, (long)request, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p < 0 && p > -4096)
            return (void *)0;
        heap_cur = (unsigned char *)p;
        heap_end = heap_cur + request;
    }
    void *out = heap_cur;
    heap_cur += n;
    return out;
}

__attribute__((visibility("default"))) void free(void *p)
{
    (void)p;
}

__attribute__((visibility("default"))) int printf(const char *fmt, ...)
{
    (void)fmt;
    static const char msg[] = "[ssc android libc shim] target printf called\n";
    shim_write(msg, sizeof(msg) - 1);
    return (int)(sizeof(msg) - 1);
}

__attribute__((visibility("default"))) int __cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
    (void)func;
    (void)arg;
    (void)dso;
    return 0;
}

__attribute__((visibility("default"))) void __cxa_finalize(void *dso)
{
    (void)dso;
}

__attribute__((visibility("default"))) int __register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void *dso)
{
    (void)prepare;
    (void)parent;
    (void)child;
    (void)dso;
    return 0;
}

__attribute__((noreturn, visibility("default"))) void __stack_chk_fail(void)
{
    static const char msg[] = "[ssc android libc shim] target stack check failed\n";
    shim_write(msg, sizeof(msg) - 1);
    (void)raw_syscall3(94, 127, 0, 0);
    __builtin_unreachable();
}
