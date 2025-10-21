/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: formatted printing
 * format_to_str() converts a format into a C string:
 * e.g., converts ("%s-%d", "egos", 2000) to "egos-2000".
 * term_write() prints the converted C string to the screen.
 */

#include "egos.h"
#include "servers.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

/* forward */
void ulltoa(unsigned long long val, char* out, int base);

void format_to_str(char* out, const char* fmt, va_list args) {
    for (out[0] = 0; *fmt != '\0'; fmt++) {
        if (*fmt != '%') {
            strncat(out, fmt, 1);
        } else {
            fmt++;
            if (*fmt == 's') {
                strcat(out, va_arg(args, char*));
            } else if (*fmt == 'd') {
                itoa(va_arg(args, int), out + strlen(out), 10);
            } else if (*fmt == 'x') {
                itoa(va_arg(args, int), out + strlen(out), 16);
            }
            /* Extended format support: %c, %u, %p, %lld, %llu, %llx */
            else if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                char tmpc[2]; tmpc[0] = c; tmpc[1] = '\0';
                strcat(out, tmpc);
            } else if (*fmt == 'u') {
                unsigned int v = va_arg(args, unsigned int);
                /* reuse itoa for values that fit in int; cast to int is ok
                 * because itoa prints decimal and unsigned values typically
                 * used here are small. For safety use ulltoa for full range.
                 */
                /* implement small unsigned conversion via ulltoa */
                char tmp[32];
                ulltoa((unsigned long long)v, tmp, 10);
                strcat(out, tmp);
            }
            else if (*fmt == 'p') {
                void* p = va_arg(args, void*);
                char tmp[32];
                strcat(out, "0x");
                ulltoa((unsigned long long)p, tmp, 16);
                strcat(out, tmp);
            } else if (*fmt == 'l') {
                /* support long-long formats: ll[d|u|x] */
                fmt++;
                if (*fmt == 'l') {
                    fmt++;
                    if (*fmt == 'd') {
                        long long v = va_arg(args, long long);
                        if (v < 0) {
                            strcat(out, "-");
                            char tmp[32];
                            ulltoa((unsigned long long)(-v), tmp, 10);
                            strcat(out, tmp);
                        } else {
                            char tmp[32];
                            ulltoa((unsigned long long)v, tmp, 10);
                            strcat(out, tmp);
                        }
                    } else if (*fmt == 'u') {
                        unsigned long long v = va_arg(args, unsigned long long);
                        char tmp[32];
                        ulltoa(v, tmp, 10);
                        strcat(out, tmp);
                    } else if (*fmt == 'x' || *fmt == 'X') {
                        unsigned long long v = va_arg(args, unsigned long long);
                        char tmp[32];
                        ulltoa(v, tmp, 16);
                        strcat(out, tmp);
                    } else {
                        /* Unknown ll format: skip */
                    }
                } else {
                    /* Unsupported single 'l' format: skip processing so that
                     * next characters are treated literally. */
                    if (*fmt != '\0') strncat(out, fmt, 1);
                }
            }
            /* Student's code goes here (Hello, World!). */

            /* Handle format %c, %u, %p, %lld, %llu and %llx. */

            /* Student's code ends here. */
        }
    }
}

/* Helper: convert unsigned long long to string in given base (10 or 16).
 * Writes a null-terminated string into out (assumed large enough).
 */
void ulltoa(unsigned long long val, char* out, int base) {
    char buf[64];
    int i = 0;
    if (val == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (val != 0) {
        int digit = val % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
        val /= base;
    }
    /* reverse */
    int j = 0;
    while (i > 0) {
        out[j++] = buf[--i];
    }
    out[j] = '\0';
}

#define LOG(prefix, suffix)                                                    \
    char buf[512];                                                             \
    strcpy(buf, prefix);                                                       \
    va_list args;                                                              \
    va_start(args, format);                                                    \
    format_to_str(buf + strlen(prefix), format, args);                         \
    va_end(args);                                                              \
    strcat(buf, suffix);                                                       \
    term_write(buf, strlen(buf));

int my_printf(const char* format, ...) { LOG("", ""); }

int INFO(const char* format, ...) { LOG("[INFO] ", "\n\r") }

int FATAL(const char* format, ...) {
    LOG("\x1B[1;31m[FATAL] ", "\x1B[1;0m\n\r") /* \x1B[1;31m means red. */
    while (1);
}

int SUCCESS(const char* format, ...) {
    LOG("\x1B[1;32m[SUCCESS] ", "\x1B[1;0m\n\r") /* \x1B[1;32m means green. */
}

int CRITICAL(const char* format, ...) {
    LOG("\x1B[1;33m[CRITICAL] ", "\x1B[1;0m\n\r") /* \x1B[1;33m means yellow. */
}
