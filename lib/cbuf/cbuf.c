/*
 * Copyright (c) 2008-2015 Travis Geiselbrecht
 * Copyright 2020-2021 NXP
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
#include <stdlib.h>
#include <debug.h>
#include <trace.h>
#include <pow2.h>
#include <string.h>
#include <assert.h>
#include <lib/cbuf.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>

#define LOCAL_TRACE 0

#define INC_POINTER(cbuf, ptr, inc) \
    (cbuf)->len_pow2 ? \
        modpow2(((ptr) + (inc)), (cbuf)->len_pow2):\
            ((ptr) + (inc)) % (cbuf)->len

#define DEC_POINTER(cbuf, ptr, inc) \
    (cbuf)->len_pow2 ? \
        modpow2(((ptr) - (inc)), (cbuf)->len_pow2):\
            ((ptr) - (inc)) % (cbuf)->len

void cbuf_initialize(cbuf_t *cbuf, size_t len)
{
    cbuf_initialize_etc(cbuf, len, malloc(len));
}

static void _cbuf_set_size(cbuf_t *cbuf, size_t len)
{
    DEBUG_ASSERT(len > 0);

    if (ispow2(len) == false) {
        TRACEF("Using circular buffer without a pow2 length degrades the performance\n");
    }
    cbuf->len_pow2 = 0;
    cbuf->len = len;
    if (ispow2(len))
        cbuf->len_pow2 = log2_uint(len);
}

void cbuf_initialize_etc(cbuf_t *cbuf, size_t len, void *buf)
{
    DEBUG_ASSERT(cbuf);
    cbuf->head = 0;
    cbuf->tail = 0;
    cbuf->len_max = len;

    _cbuf_set_size(cbuf, len);

    cbuf->buf = buf;
    cbuf->no_event = false;
    cbuf->is_reset = false;
    cbuf->flags = CBUF_FLAG_DEFAULT;

    event_init(&cbuf->event, false, 0);
    spin_lock_init(&cbuf->lock);

    LTRACEF("len %u, len_pow2 %u\n", cbuf->len, cbuf->len_pow2);
}

void cbuf_adjust_size(cbuf_t *cbuf, size_t len)
{
    DEBUG_ASSERT(cbuf->len_max >= len);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    cbuf->head = 0;
    cbuf->tail = 0;
    _cbuf_set_size(cbuf, len);

    spin_unlock_irqrestore(&cbuf->lock, state);
}

size_t cbuf_space_avail(cbuf_t *cbuf)
{
    if (cbuf->len_pow2) {
        uint consumed = modpow2((uint)(cbuf->head - cbuf->tail), cbuf->len_pow2);
        return valpow2(cbuf->len_pow2) - consumed - 1;
    } else {
        uint consumed = (cbuf->head - cbuf->tail) % cbuf->len;
        return cbuf->len - consumed - 1;
    }
}

size_t cbuf_space_used(cbuf_t *cbuf)
{
    if (cbuf->len_pow2) {
        return modpow2((uint)(cbuf->head - cbuf->tail), cbuf->len_pow2);
    } else {
        return (cbuf->head - cbuf->tail) % cbuf->len;
    }
}

static size_t cbuf_get_contiguous_space(cbuf_t *cbuf, size_t len, size_t pos)
{
    size_t write_len;
    if (cbuf->head >= cbuf->tail) {
        if (cbuf->tail == 0) {
            // Special case - if tail is at position 0, we can't write all
            // the way to the end of the buffer. Otherwise, head ends up at
            // 0, head == tail, and buffer is considered "empty" again.
            write_len =
                MIN(cbuf->len - cbuf->head - 1, len - pos);
        } else {
            // Write to the end of the buffer.
            write_len =
                MIN(cbuf->len - cbuf->head, len - pos);
        }
    } else {
        // Write from head to tail-1.
        write_len = MIN(cbuf->tail - cbuf->head - 1, len - pos);
    }

    return write_len;
}

static size_t cbuf_write_wo_lock(cbuf_t *cbuf, const char *buf, size_t len, bool canreschedule)
{
    size_t write_len;
    size_t pos = 0;
    bool enable = cbuf_is_sw_writer(cbuf);

    while (pos < len && cbuf_space_avail(cbuf) > 0) {
        write_len = cbuf_get_contiguous_space(cbuf, len, pos);

        // if it's full, abort and return how much we've written
        if (write_len == 0) {
            break;
        }

        if (NULL == buf) {
            if ((cbuf->is_reset == false) && (enable)) {
                memset(cbuf->buf + cbuf->head, 0, write_len);
            }
        } else {
            if (enable) {
                memcpy(cbuf->buf + cbuf->head, buf + pos, write_len);
            }
            cbuf->is_reset = false;
        }

        if (cbuf_is_cacheable(cbuf) && cbuf_is_hw_reader(cbuf)) {
            arch_clean_invalidate_cache_range(
                                    (vaddr_t) cbuf->buf + cbuf->head, write_len);
        }
        cbuf->head = INC_POINTER(cbuf, cbuf->head, write_len);
        pos += write_len;
    }

    if (!cbuf->no_event && cbuf->head != cbuf->tail)
        event_signal(&cbuf->event, false);

    return pos;
}

size_t cbuf_write(cbuf_t *cbuf, const void *_buf, size_t len, bool canreschedule)
{

    const char *buf = (const char *)_buf;
    LTRACEF("len %zd\n", len);

    DEBUG_ASSERT(cbuf);
    DEBUG_ASSERT(len < cbuf->len);

    size_t pos = 0;

    if (!cbuf_writer_use_max_chunk(cbuf) || !buf) {
        spin_lock_saved_state_t state;
        spin_lock_irqsave(&cbuf->lock, state);

        pos = cbuf_write_wo_lock(cbuf, buf, len, canreschedule);

        spin_unlock_irqrestore(&cbuf->lock, state);
        goto out;
    }

    size_t remaining = len;
    int loop = len / CBUF_WRITE_MAX_CHUNK;
    loop += (len % CBUF_WRITE_MAX_CHUNK != 0);

    while (loop--) {
        spin_lock_saved_state_t state;
        spin_lock_irqsave(&cbuf->lock, state);

        size_t written = cbuf_write_wo_lock(
            cbuf, buf,
            (remaining > CBUF_WRITE_MAX_CHUNK) ? CBUF_WRITE_MAX_CHUNK : remaining,
            canreschedule);
        spin_unlock_irqrestore(&cbuf->lock, state);

        if (written == 0)
            goto out;

        pos += written;
        remaining -= written;
        buf += written;
    }

out:
    // XXX convert to only rescheduling if
    if (canreschedule)
        thread_preempt();

    return pos;
}

static size_t cbuf_get_contiguous_used(cbuf_t *cbuf, size_t buflen, size_t pos)
{
    size_t read_len;
    if (cbuf->head > cbuf->tail) {
        // simple case where there is no wraparound
        read_len = MIN(cbuf->head - cbuf->tail, buflen - pos);
    } else {
        // read to the end of buffer in this pass
        read_len = MIN(cbuf->len - cbuf->tail, buflen - pos);
    }
    return read_len;
}

size_t cbuf_read_wo_lock(cbuf_t *cbuf, char *buf, size_t buflen, bool block)
{
    bool enable = cbuf_is_sw_reader(cbuf);
    // see if there's data available
    size_t ret = 0;
    if (cbuf->tail != cbuf->head) {
        size_t pos = 0;

        // loop until we've read everything we need
        // at most this will make two passes to deal with wraparound
        while (pos < buflen && cbuf->tail != cbuf->head) {
            size_t read_len = cbuf_get_contiguous_used(cbuf, buflen, pos);

            if (cbuf_is_cacheable(cbuf) && cbuf_is_hw_writer(cbuf)) {
                arch_invalidate_cache_range(
                                    (vaddr_t) cbuf->buf + cbuf->tail, read_len);
            }
            // Only perform the copy if a buf was supplied
            if (NULL != buf) {
                if (enable)
                    memcpy(buf + pos, cbuf->buf + cbuf->tail, read_len);
            }

            cbuf->tail = INC_POINTER(cbuf, cbuf->tail, read_len);
            pos += read_len;
        }

        if (!cbuf->no_event && cbuf->tail == cbuf->head) {
            DEBUG_ASSERT(pos > 0);
            // we've emptied the buffer, unsignal the event
            event_unsignal(&cbuf->event);
        }

        ret = pos;
    }

    return ret;
}

size_t cbuf_read(cbuf_t *cbuf, void *_buf, size_t buflen, bool block)
{
    size_t ret = 0;
    char *buf = (char *)_buf;

    DEBUG_ASSERT(cbuf);

retry:
    // block on the cbuf outside of the lock, which may
    // unblock us early and we'll have to double check below
    if (!cbuf->no_event && block)
        event_wait(&cbuf->event);

    if (!cbuf_reader_use_max_chunk(cbuf) || !buf) {
        spin_lock_saved_state_t state;
        spin_lock_irqsave(&cbuf->lock, state);

        ret = cbuf_read_wo_lock(cbuf, buf, buflen, block);

        spin_unlock_irqrestore(&cbuf->lock, state);
        goto out;
    }

    size_t remaining = buflen;
    int loop = buflen / CBUF_READ_MAX_CHUNK;
    loop += (buflen % CBUF_READ_MAX_CHUNK != 0);

    while (loop--) {
        spin_lock_saved_state_t state;
        spin_lock_irqsave(&cbuf->lock, state);

        size_t read = cbuf_read_wo_lock(
            cbuf, buf,
            (remaining > CBUF_READ_MAX_CHUNK) ? CBUF_READ_MAX_CHUNK : remaining,
            block);
        spin_unlock_irqrestore(&cbuf->lock, state);

        if (read == 0)
            goto out;

        ret += read;
        remaining -= read;
        buf += read;
    }

out:
    // we apparently blocked but raced with another thread and found no data, retry
    if (block && ret == 0)
        goto retry;

    return ret;
}

void cbuf_trash(cbuf_t *cbuf, size_t len)
{
    LTRACEF("len %zd\n", len);

    DEBUG_ASSERT(cbuf);

    /* Trashing does not make sense if there is at least one
     * hw producer or consumer
     */
    if (cbuf_is_sw_writer(cbuf) && (cbuf_is_sw_reader(cbuf)))
        return;

    DEBUG_ASSERT(len < cbuf->len);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    cbuf->head = INC_POINTER(cbuf, cbuf->head, len);
    cbuf->tail = INC_POINTER(cbuf, cbuf->tail, len);

    spin_unlock_irqrestore(&cbuf->lock, state);
}

void cbuf_skip(cbuf_t *cbuf, bool is_write, size_t len)
{
    DEBUG_ASSERT(len < cbuf->len);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);
    if (is_write)
        cbuf->head = INC_POINTER(cbuf, cbuf->head, len);
    else
        cbuf->tail = INC_POINTER(cbuf, cbuf->tail, len);

    spin_unlock_irqrestore(&cbuf->lock, state);
}

size_t cbuf_rewind(cbuf_t *cbuf)
{
    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    size_t len = cbuf_space_used(cbuf);

    cbuf->head = cbuf->tail;

    spin_unlock_irqrestore(&cbuf->lock, state);

    return len;
}

size_t cbuf_rewind_len(cbuf_t *cbuf, size_t len)
{
    DEBUG_ASSERT(len < cbuf->len);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    size_t max = cbuf_space_used(cbuf);
    if (len > max)
        len = max;

    cbuf->head = DEC_POINTER(cbuf, cbuf->head, len);

    spin_unlock_irqrestore(&cbuf->lock, state);

    return len;
}

size_t cbuf_peek(cbuf_t *cbuf, iovec_t *regions)
{
    DEBUG_ASSERT(cbuf && regions);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    size_t ret = cbuf_space_used(cbuf);
    size_t sz  = cbuf_size(cbuf);

    DEBUG_ASSERT(cbuf->tail < sz);
    DEBUG_ASSERT(ret <= sz);

    regions[0].iov_base = ret ? (cbuf->buf + cbuf->tail) : NULL;
    if (ret + cbuf->tail > sz) {
        regions[0].iov_len  = sz - cbuf->tail;
        regions[1].iov_base = cbuf->buf;
        regions[1].iov_len  = ret - regions[0].iov_len;
    } else {
        regions[0].iov_len  = ret;
        regions[1].iov_base = NULL;
        regions[1].iov_len  = 0;
    }

    spin_unlock_irqrestore(&cbuf->lock, state);
    return ret;
}

size_t cbuf_write_char(cbuf_t *cbuf, char c, bool canreschedule)
{
    DEBUG_ASSERT(cbuf);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    size_t ret = 0;
    if (cbuf_space_avail(cbuf) > 0) {
        cbuf->buf[cbuf->head] = c;

        cbuf->head = INC_POINTER(cbuf, cbuf->head, 1);
        ret = 1;

        if (!cbuf->no_event && cbuf->head != cbuf->tail)
            event_signal(&cbuf->event, canreschedule);
    }

    spin_unlock_irqrestore(&cbuf->lock, state);

    return ret;
}

size_t cbuf_read_char(cbuf_t *cbuf, char *c, bool block)
{
    DEBUG_ASSERT(cbuf);
    DEBUG_ASSERT(c);

retry:
    if (!cbuf->no_event && block)
        event_wait(&cbuf->event);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    // see if there's data available
    size_t ret = 0;
    if (cbuf->tail != cbuf->head) {

        *c = cbuf->buf[cbuf->tail];
        cbuf->tail = INC_POINTER(cbuf, cbuf->tail, 1);

        if (!cbuf->no_event && cbuf->tail == cbuf->head) {
            // we've emptied the buffer, unsignal the event
            event_unsignal(&cbuf->event);
        }

        ret = 1;
    }

    spin_unlock_irqrestore(&cbuf->lock, state);

    if (block && ret == 0)
        goto retry;

    return ret;
}

void cbuf_reset_with_zero(cbuf_t *cbuf)
{
    memset(cbuf->buf, 0, cbuf_size(cbuf));
    if (cbuf_is_sw_writer(cbuf) && (cbuf_is_sw_reader(cbuf))) {
        cbuf_reset(cbuf);
    } else {
        cbuf_reset_indexes(cbuf);
        if (cbuf_is_cacheable(cbuf) && cbuf_is_hw_reader(cbuf))
            arch_clean_invalidate_cache_range((vaddr_t) cbuf->buf, cbuf_size(cbuf));
    }
    cbuf->is_reset = true;
}

void cbuf_reset_indexes(cbuf_t *cbuf)
{
    cbuf_reset(cbuf);
    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);
    cbuf->head = cbuf->tail = 0;
    spin_unlock_irqrestore(&cbuf->lock, state);
}

