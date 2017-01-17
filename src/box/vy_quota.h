#ifndef INCLUDES_TARANTOOL_BOX_VY_QUOTA_H
#define INCLUDES_TARANTOOL_BOX_VY_QUOTA_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Quota used for accounting and limiting memory consumption
 * in the vinyl engine. It is NOT multi-threading safe.
 */
struct vy_quota {
	/**
	 * Memory limit. Once hit, new transactions are
	 * throttled until memory is reclaimed.
	 */
	size_t limit;
	/**
	 * Memory watermark. Exceeding it does not result in
	 * throttling new transactions, but it does trigger
	 * background memory reclaim.
	 */
	size_t watermark;
	/** Current memory consumption. */
	size_t used;
	/** Maximal time to wait for quota to release, in seconds. */
	double timeout;
	/** Called when quota is consumed and used >= watermark. */
	void (*watermark_cb)(void *arg);
	/**
	 * Called when quota is consumed and used >= limit.
	 *
	 * This function is supposed to put the current fiber to
	 * sleep until release_cb() wakes it up. It is passed the
	 * maximal time to wait. It should return the time left
	 * or 0 on timeout.
	 */
	double (*throttle_cb)(void *arg, double timeout);
	/** Called when quota is released and used < limit. */
	void (*release_cb)(void *arg);
	/** Argument passed to cb. */
	void *cb_arg;
};

static inline void
vy_quota_init(struct vy_quota *q, int64_t limit, double timeout,
	      void (*watermark_cb)(void *arg),
	      double (*throttle_cb)(void *arg, double timeout),
	      void (*release_cb)(void *arg), void *cb_arg)
{
	q->limit = limit;
	q->watermark = limit;
	q->used = 0;
	q->timeout = timeout;
	q->watermark_cb = watermark_cb;
	q->throttle_cb = throttle_cb;
	q->release_cb = release_cb;
	q->cb_arg = cb_arg;
}

/**
 * Return true if memory reclaim should be triggered.
 */
static inline bool
vy_quota_is_exceeded(struct vy_quota *q)
{
	return q->used >= q->watermark;
}

/**
 * Given the rate of memory consumption vs release and
 * the size of memory chunk that will be reclaimed next,
 * compute the optimal watermark.
 */
static inline void
vy_quota_update_watermark(struct vy_quota *q, size_t chunk_size,
			  size_t use_rate, size_t release_rate)
{
	/*
	 * The gap between the watermark and the hard limit
	 * is set to such a value that should allow us to dumpe
	 * the next range before the hard limit is hit, basing
	 * on average tx rate and disk bandwidth.
	 */
	size_t gap = (double)chunk_size * use_rate / release_rate;
	if (gap < q->limit)
		q->watermark = q->limit - gap;
	else
		q->watermark = 0;
}

/**
 * Consume @size bytes of memory. Throttle the caller if
 * the limit is exceeded.
 *
 * Returns 0 on success, -1 on timeout, in which case
 * throttle_cb() is responsible for setting diag.
 */
static inline int
vy_quota_use(struct vy_quota *q, size_t size)
{
	q->used += size;
	if (q->used >= q->watermark)
		q->watermark_cb(q->cb_arg);
	double timeout = q->timeout;
	while (q->used >= q->limit && timeout > 0)
		timeout = q->throttle_cb(q->cb_arg, timeout);
	return timeout > 0 ? 0 : -1;
}

/**
 * Consume @size bytes of memory. In contrast to vy_quota_use()
 * this function does not throttle the caller.
 */
static inline void
vy_quota_force_use(struct vy_quota *q, size_t size)
{
	q->used += size;
}

/**
 * Release @size bytes of memory.
 */
static inline void
vy_quota_release(struct vy_quota *q, size_t size)
{
	assert(q->used >= size);
	q->used -= size;
	if (q->used < q->limit)
		q->release_cb(q->cb_arg);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_QUOTA_H */
