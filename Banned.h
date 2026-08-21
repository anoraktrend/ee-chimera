#ifndef BANNED_H
#define BANNED_H

/*
 * Banned.h - compile-time ban list for unsafe APIs.
 *
 * This header is force-included by the Makefile (-include Banned.h) before
 * every translation unit. Each banned function is #undef'd and replaced by
 * a macro that expands to a call of an eponymous stub declared with
 * clang's error attribute, so ANY use fails compilation with an explicit
 * reason instead of silently introducing a vulnerability.
 *
 * The libc headers are included HERE (before the bans are applied) so that
 * later #include <string.h>-style directives in source files resolve to
 * no-ops and never collide with the replacement macros.
 *
 * Escape hatch for legitimate exotic cases: call as (strcpy)(a, b) --
 * parenthesizing suppresses function-like macro expansion -- but expect
 * review pushback; the correct fix is always snprintf()/memcpy().
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__clang__) || (defined(__GNUC__) && defined(__has_attribute))
#  if __has_attribute(error)
#    define EE_BANNED(msg) __attribute__((error("banned API: " msg)))
#  else
#    define EE_BANNED(msg)
#  endif
#else
#  define EE_BANNED(msg)
#endif

/* --- <string.h> ------------------------------------------------------- */

EE_BANNED("strcpy() performs no bounds checking and cannot prevent buffer "
          "overflows; use snprintf(), memcpy(), or strlcpy() with an "
          "explicit size")
char *ee_banned_strcpy(char *, const char *);
#undef strcpy
#define strcpy(dst, src) ee_banned_strcpy(dst, src)

EE_BANNED("strcat() performs no bounds checking and cannot prevent buffer "
          "overflows; build into a sized buffer with snprintf()")
char *ee_banned_strcat(char *, const char *);
#undef strcat
#define strcat(dst, src) ee_banned_strcat(dst, src)

EE_BANNED("strncpy() does not guarantee NUL termination on truncation, "
          "zero-pads wastefully, and is easy to misuse; use snprintf() or "
          "memcpy() plus an explicit terminator")
char *ee_banned_strncpy(char *, const char *, size_t);
#undef strncpy
#define strncpy(dst, src, n) ee_banned_strncpy(dst, src, n)

EE_BANNED("strncat()'s 'n' bounds the bytes copied from src, not the "
          "destination capacity, so it still overflows; use snprintf()")
char *ee_banned_strncat(char *, const char *, size_t);
#undef strncat
#define strncat(dst, src, n) ee_banned_strncat(dst, src, n)

EE_BANNED("strtok() is not reentrant/thread-safe and mutates its input; "
          "tokenize with strspn()/strcspn() or strtok_r()")
char *ee_banned_strtok(char *, const char *);
#undef strtok
#define strtok(s, delim) ee_banned_strtok(s, delim)

EE_BANNED("strtok_r() mutates its input and obscures ownership; prefer "
          "index-based tokenizing with strspn()/strcspn()")
char *ee_banned_strtok_r(char *, const char *, char **);
#undef strtok_r
#define strtok_r(s, delim, save) ee_banned_strtok_r(s, delim, save)

/* --- <stdio.h> -------------------------------------------------------- */

EE_BANNED("gets() has no way to bound input and is unconditionally "
          "unsafe; it was removed from the C standard - use fgets()")
char *ee_banned_gets(char *);
#undef gets
#define gets(buf) ee_banned_gets(buf)

EE_BANNED("sprintf() cannot bound its output; use snprintf()")
int ee_banned_sprintf(char *, const char *, ...);
#undef sprintf
#define sprintf(...) ee_banned_sprintf(__VA_ARGS__)

EE_BANNED("vsprintf() cannot bound its output; use vsnprintf()")
int ee_banned_vsprintf(char *, const char *, va_list);
#undef vsprintf
#define vsprintf(...) ee_banned_vsprintf(__VA_ARGS__)

/* --- <time.h> --------------------------------------------------------- */

EE_BANNED("gmtime() returns a shared static buffer that is clobbered by "
          "every other caller; use gmtime_r()")
struct tm *ee_banned_gmtime(const time_t *);
#undef gmtime
#define gmtime(t) ee_banned_gmtime(t)

EE_BANNED("localtime() returns a shared static buffer that is clobbered "
          "by every other caller; use localtime_r()")
struct tm *ee_banned_localtime(const time_t *);
#undef localtime
#define localtime(t) ee_banned_localtime(t)

EE_BANNED("ctime() is locale-naive, timezone-implicit, and writes to a "
          "shared static buffer; format with strftime()/localtime_r()")
char *ee_banned_ctime(const time_t *);
#undef ctime
#define ctime(t) ee_banned_ctime(t)

EE_BANNED("asctime() has a fixed 26-byte output and no overflow "
          "reporting; format with strftime()")
char *ee_banned_asctime(const struct tm *);
#undef asctime
#define asctime(tm) ee_banned_asctime(tm)

EE_BANNED("asctime_r()/ctime_r() have fixed-size outputs and no overflow "
          "reporting; format with strftime()")
char *ee_banned_ctime_r(const time_t *, char *);
#undef ctime_r
#define ctime_r(t, buf) ee_banned_ctime_r(t, buf)
#undef asctime_r
#define asctime_r(tm, buf) ee_banned_ctime_r(tm, buf)

/* --- <stdlib.h> ------------------------------------------------------- */

EE_BANNED("mktemp() generates predictably-named files and is race-prone; "
          "use mkstemp() or mkdtemp()")
char *ee_banned_mktemp(char *);
#undef mktemp
#define mktemp(tmpl) ee_banned_mktemp(tmpl)

#endif /* BANNED_H */
