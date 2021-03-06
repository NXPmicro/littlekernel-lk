/*
 * Copyright (c) 2009-2013 Travis Geiselbrecht
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
#pragma once

#include <compiler.h>
#include <sys/types.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <iovec.h>

__BEGIN_CDECLS

typedef struct cbuf {
    uint head;
    uint tail;
    uint len_max;
    uint len;
    uint len_pow2;
    char *buf;
    event_t event;
    spin_lock_t lock;
    bool no_event;
    bool is_reset;
    uint flags;
} cbuf_t;

#define CBUF_FLAG_NO_EVENT          (1 << 0)
#define CBUF_FLAG_IS_RESET          (1 << 1)
#define CBUF_FLAG_SW_IS_WRITER      (1 << 2)
#define CBUF_FLAG_SW_IS_READER      (1 << 3)
#define CBUF_FLAG_BUF_IS_CACHEABLE  (1 << 4)
#define CBUF_FLAG_USE_MAX_CHUNK_R   (1 << 5)
#define CBUF_FLAG_USE_MAX_CHUNK_W   (1 << 6)
#define CBUF_FLAG_USE_MAX_CHUNK_RW  \
    (CBUF_FLAG_USE_MAX_CHUNK_R | CBUF_FLAG_USE_MAX_CHUNK_W)

#define CBUF_READ_MAX_CHUNK         (16 << 10)
#define CBUF_WRITE_MAX_CHUNK        (16 << 10)

#define CBUF_FLAG_DEFAULT ( CBUF_FLAG_SW_IS_WRITER \
                            | CBUF_FLAG_SW_IS_READER \
                            | CBUF_FLAG_BUF_IS_CACHEABLE )

static inline bool cbuf_reader_use_max_chunk(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_USE_MAX_CHUNK_R);
}

static inline bool cbuf_writer_use_max_chunk(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_USE_MAX_CHUNK_W);
}

static inline bool cbuf_is_no_event(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_NO_EVENT);
}

static inline bool cbuf_is_reset(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_IS_RESET);
}

static inline bool cbuf_is_sw_writer(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_SW_IS_WRITER);
}

static inline bool cbuf_is_sw_reader(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_SW_IS_READER);
}

static inline bool cbuf_is_hw_writer(cbuf_t *cbuf)
{
    return !(cbuf_is_sw_writer(cbuf));
}

static inline bool cbuf_is_hw_reader(cbuf_t *cbuf)
{
    return !(cbuf_is_sw_reader(cbuf));
}

static inline bool cbuf_is_cacheable(cbuf_t *cbuf)
{
    return !!(cbuf->flags & CBUF_FLAG_BUF_IS_CACHEABLE);
}

static inline void cbuf_change_flag(cbuf_t *cbuf, uint flag, bool set)
{
    spin_lock_saved_state_t lock_state;
    spin_lock_irqsave(&cbuf->lock, lock_state);

    if (set)
        cbuf->flags |= flag;
    else
        cbuf->flags &= ~flag;

    smp_wmb();

    spin_unlock_irqrestore(&cbuf->lock, lock_state);
}

static inline void cbuf_set_flag(cbuf_t *cbuf, uint flag)
{
    cbuf_change_flag(cbuf, flag, true);
}

static inline void cbuf_clear_flag(cbuf_t *cbuf, uint flag)
{
    cbuf_change_flag(cbuf, flag, false);
}

/**
 * cbuf_initialize
 *
 * Initialize a cbuf structure, mallocing the underlying data buffer in the
 * process.  Make sure that the buffer has enough space for at least len bytes.
 *
 * @param[in] cbuf A pointer to the cbuf structure to allocate.
 * @param[in] len The minimum number of bytes for the underlying data buffer.
 * Must be a power of two.
 */
void cbuf_initialize(cbuf_t *cbuf, size_t len);

/**
 * cbuf_initalize_etc
 *
 * Initialize a cbuf structure using the supplied buffer for internal storage.
 *
 * @param[in] cbuf A pointer to the cbuf structure to allocate.
 * @param[in] len The size of the supplied buffer, in bytes.  Must be a power
 * of two.
 * @param[in] buf A pointer to the memory to be used for internal storage.
 */
void cbuf_initialize_etc(cbuf_t *cbuf, size_t len, void *buf);

/**
 * cbuf_adjust_size
 *
 * Adjust the size of the circular buffer already initialized
 *
 * @param[in] cbuf A pointer to the cbuf structure to allocate.
 * @param[in] len The new size of the buffer, in bytes. Must be less than
 * initial buffer size.
 */
void cbuf_adjust_size(cbuf_t *cbuf, size_t len);

/**
 * cbuf_read
 *
 * Read up to buflen bytes in to the supplied buffer.
 *
 * @param[in] cbuf The cbuf instance to read from.
 * @param[in] buf A pointer to a buffer to read data into.  If NULL, cbuf_read
 * will skip up to the next buflen bytes from the cbuf.
 * @param[in] buflen The maximum number of bytes to read from the cbuf.
 * @param[in] block When true, will cause the caller to block until there is at
 * least one byte available to read from the cbuf.
 *
 * @return The actual number of bytes which were read (or skipped).
 */
size_t cbuf_read(cbuf_t *cbuf, void *buf, size_t buflen, bool block);

/**
 * cbuf_peek
 *
 * Peek at the data available for read in the cbuf right now.  Does not actually
 * consume the data, it just fills out a pair of iovec structures describing the
 * (up to) two contiguous regions currently available for read.
 *
 * @param[in] cbuf The cbuf instance to write to.
 * @param[out] A pointer to two iovec structures to hold the contiguous regions
 * for read.  NOTE: regions must point to a chunk of memory which is at least
 * sizeof(iovec_t) * 2 bytes long.
 *
 * @return The number of bytes which were written (or skipped).
 */
size_t cbuf_peek(cbuf_t *cbuf, iovec_t *regions);

/**
 * cbuf_rewind
 *
 * Remove all latest written bytes by moving head pointer to tail.
 *
 * @param[in] cbuf The cbuf instance to apply the operation.
 *
 * @return The number of removed bytes.
 */
size_t cbuf_rewind(cbuf_t *cbuf);

/**
 * cbuf_rewind_len
 *
 * iRemove latest written bytes by decrementing the head pointer.
 *
 * @param[in] cbuf The cbuf instance to apply the operation.
 * @param[in] len How many bytes should the pointer be decremented.
 *
 * @return The number of removed bytes.
 */
size_t cbuf_rewind_len(cbuf_t *cbuf, size_t len);

void cbuf_trash(cbuf_t *cbuf, size_t len);

/**
 * cbuf_skip
 *
 * Increment the tail or head pointer for len bytes.
 *
 * @param[in] cbuf The cbuf instance to apply the operation.
 * @param[in] is_write True for increment the head pointer, False for the tail
 * @param[in] len How many bytes should the pointer be incremented
 *
 * @return The number of bytes which were written (or skipped).
 */

void cbuf_skip(cbuf_t *cbuf, bool is_write, size_t len);

/**
 * cbuf_write
 *
 * Write up to len bytes from the the supplied buffer into the cbuf.
 *
 * @param[in] cbuf The cbuf instance to write to.
 * @param[in] buf A pointer to a buffer to read data from.  If NULL, cbuf_write
 * will skip up to the next len bytes in the cbuf, filling with zeros instead of
 * supplied data.
 * @param[in] len The maximum number of bytes to write to the cbuf.
 * @param[in] canreschedule Rescheduling policy passed through to the internal
 * event when signaling the event to indicate that there is now data in the
 * buffer to be read.
 *
 * @return The number of bytes which were written (or skipped).
 */
size_t cbuf_write(cbuf_t *cbuf, const void *buf, size_t len, bool canreschedule);

/**
 * cbuf_space_avail
 *
 * @param[in] cbuf The cbuf instance to query
 *
 * @return The number of free space available in the cbuf (IOW - the maximum
 * number of bytes which can currently be written)
 */
size_t cbuf_space_avail(cbuf_t *cbuf);

/**
 * cbuf_space_used
 *
 * @param[in] cbuf The cbuf instance to query
 *
 * @return The number of used bytes in the cbuf (IOW - the maximum number of
 * bytes which can currently be read).
 */
size_t cbuf_space_used(cbuf_t *cbuf);

/**
 * cbuf_size
 *
 * @param[in] cbuf The cbuf instance to query
 *
 * @return The size of the cbuf's underlying data buffer.
 */
static inline size_t cbuf_size(cbuf_t *cbuf)
{
    return cbuf->len;
}

/**
 * cbuf_reset
 *
 * Reset the cbuf instance, discarding any data which may be in the buffer at
 * the moment.
 *
 * @param[in] cbuf The cbuf instance to reset.
 */
static inline void cbuf_reset(cbuf_t *cbuf)
{
    cbuf_read(cbuf, NULL, cbuf_size(cbuf), false);
}

/**
 * cbuf_reset_and_zero
 *
 * Reset the cbuf instance, discarding any data which may be in the buffer at
 * the moment, and fill the buffer with 0.
 *
 * @param[in] cbuf The cbuf instance to reset and zero.
 */
void cbuf_reset_with_zero(cbuf_t *cbuf);

/**
 * cbuf_reset_indexes
 *
 * Reset the cbuf instance, discarding any data which may be in the buffer at
 * the moment, and move the head and tail pointers to buffer base address.
 *
 * @param[in] cbuf The cbuf instance to reset.
 */
void cbuf_reset_indexes(cbuf_t *cbuf);

/* special cases for dealing with a single char of data */
size_t cbuf_read_char(cbuf_t *cbuf, char *c, bool block);
size_t cbuf_write_char(cbuf_t *cbuf, char c, bool canreschedule);

__END_CDECLS

