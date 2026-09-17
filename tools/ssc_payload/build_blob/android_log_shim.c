typedef __SIZE_TYPE__ size_t;

static long raw_write(long fd, const char *buf, size_t n)
{
    register long x0 asm("x0") = fd;
    register long x1 asm("x1") = (long)buf;
    register long x2 asm("x2") = (long)n;
    register long x8 asm("x8") = 64;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static size_t cstr_len(const char *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

__attribute__((visibility("default"))) int __android_log_write(int prio, const char *tag, const char *text)
{
    (void)prio;
    static const char prefix[] = "[ssc android log shim] ";
    raw_write(2, prefix, sizeof(prefix) - 1);
    raw_write(2, tag ? tag : "?", cstr_len(tag ? tag : "?"));
    raw_write(2, ": ", 2);
    raw_write(2, text ? text : "", cstr_len(text ? text : ""));
    raw_write(2, "\n", 1);
    return 1;
}

__attribute__((visibility("default"))) int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    return __android_log_write(prio, tag, fmt);
}
