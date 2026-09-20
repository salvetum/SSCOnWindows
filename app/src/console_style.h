/*
 * console_style.h - minimal ANSI color helpers for the CLI frontend.
 *
 * Plan: docs/dev/PLAN_DESIGN_CONSISTENCY.md, Faz A (CLI readability).
 * Rules honored here (see the plan's "Genel kural"):
 *  - Colors are only emitted when the output stream is a real console (TTY).
 *    Pipes/redirections stay plain so logs and tests keep byte-clean output.
 *  - NO_COLOR env var and the CLI's --no-color flag disable colors outright.
 *  - Tag semantics used across CLI + GUI log pane + setup.ps1:
 *      INFO (default) | OK (green) | WARN (yellow) | ERROR (red) | DATA (cyan)
 *  - No terminal font is forced; only single-width glyphs are produced.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SSCONWINDOWS_CONSOLE_STYLE_H
#define SSCONWINDOWS_CONSOLE_STYLE_H

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cstyle {

enum class Tag {
    Info,   /* default text, no paint */
    Ok,     /* green  */
    Warn,   /* yellow */
    Error,  /* red    */
    Data,   /* cyan   (diagnostic rows)   */
    Dim,    /* bright black (low emphasis) */
    Bold    /* bright white bold */
};

/* Global switch: set once per process by init(). */
inline bool g_enabled = false;

/* Turn VT processing on for stdout+stderr so ANSI escapes work on Windows
 * consoles. No-op if NO_COLOR is set or force_off (--no-color). */
inline void init(bool force_off = false) {
    if (force_off) return;
    const char *no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') return;
#ifdef _WIN32
    const HANDLE handles[] = { GetStdHandle(STD_OUTPUT_HANDLE),
                               GetStdHandle(STD_ERROR_HANDLE) };
    for (HANDLE h : handles) {
        DWORD mode = 0;
        if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            g_enabled = true;
        }
    }
#endif
}

/* Per-stream check: emit ANSI only when that stream is an interactive console. */
inline bool on(FILE *f) {
    if (!g_enabled) return false;
#ifdef _WIN32
    HANDLE h = (f == stdout) ? GetStdHandle(STD_OUTPUT_HANDLE)
              : (f == stderr) ? GetStdHandle(STD_ERROR_HANDLE)
                              : INVALID_HANDLE_VALUE;
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
#else
    (void)f;
    return false;
#endif
}

inline const char *code(Tag t) {
    switch (t) {
    case Tag::Ok:    return "\x1b[32m";
    case Tag::Warn:  return "\x1b[33m";
    case Tag::Error: return "\x1b[31m";
    case Tag::Data:  return "\x1b[36m";
    case Tag::Dim:   return "\x1b[2m";
    case Tag::Bold:  return "\x1b[1m";
    case Tag::Info:  return "\x1b[39m";
    }
    return "";
}

/* Colored printf to a stream. Emits an ANSI color only when the stream is a
 * console; otherwise byte-identical plain output. */
inline void fprint(FILE *f, Tag tag, const char *fmt, ...) {
    const bool c = on(f);
    if (c) std::fputs(code(tag), f);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    if (c) std::fputs("\x1b[0m", f);
}

} /* namespace cstyle */

#endif /* SSCONWINDOWS_CONSOLE_STYLE_H */