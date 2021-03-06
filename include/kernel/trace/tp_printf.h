/*
 * Copyright 2019 - NXP
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __TRACE_TP_PRINTF_H
#define __TRACE_TP_PRINTF_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <kernel/trace/tracepoint.h>

LK_TP(subsys_global_printf,
    LK_PARAMS(const char *str),
    LK_PARAMS(str));

#if WITH_KERNEL_TRACEPOINT

static inline void lk_trace_printf(const char *format, ...)
{
    char buffer[256] = { 0 };
    va_list args;

    va_start(args, format);
    vsprintf(buffer, format, args);
    if (buffer[strlen(buffer) - 1] == '\n')
	    buffer[strlen(buffer) - 1] = 0;
    lk_trace_subsys_global_printf(buffer);
    va_end(args);
}

#else

static inline void lk_trace_printf(const char *format, ...) { }

#endif

#endif
