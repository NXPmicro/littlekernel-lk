/*
 * Copyright (c) 2008-2015 Travis Geiselbrecht
 * Copyright 2019-2020 NXP
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

#include <compiler.h>
#include <debug.h>
#include <trace.h>
#include <kernel/mutex.h>

/* Default implementation of panic time getc/putc.
 * Just calls through to the underlying dputc/dgetc implementation
 * unless the platform overrides it.
 */
__WEAK void platform_pputc(char c)
{
    return platform_dputc(c);
}

__WEAK int platform_pgetc(char *c, bool wait)
{
    return platform_dgetc(c, wait);
}

__WEAK void platform_dputs(const char* str, size_t len)
{
    unsigned i;
    for (i = 0; i < len; i++)
        platform_pputc(str[i]);
}

static mutex_t dputs_lock = MUTEX_INITIAL_VALUE(dputs_lock);
__WEAK void platform_dputs_thread(const char* str, size_t len)
{

    mutex_acquire(&dputs_lock);
    platform_dputs(str, len);
    mutex_release(&dputs_lock);
}

__WEAK void platform_dputs_irq(const char* str, size_t len)
{
    platform_dputs(str, len);
}

__WEAK bool platform_is_console_enabled(void)
{
    return true;
}
