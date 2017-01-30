/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "tuple.h"

#include "trivia/util.h"
#include "fiber.h"

static struct mempool tuple_iterator_pool;

/**
 * Last tuple returned by public C API
 * \sa tuple_bless()
 */
struct tuple *box_tuple_last;

/**
 * A format for standalone tuples allocated on runtime arena.
 * \sa tuple_new().
 */
static struct tuple_format *tuple_format_runtime;

struct tuple *
tuple_new(const char *data, const char *end)
{
	mp_tuple_assert(data, end);
	size_t data_len = end - data;
	size_t total = sizeof(struct tuple) + data_len;

	struct tuple *tuple = (struct tuple *) malloc(total);
	if (tuple == NULL) {
		diag_set(OutOfMemory, (unsigned) total,
			 "malloc", "tuple");
		return NULL;
	}

	struct tuple_format *format = tuple_format_runtime;
	tuple->refs = 0;
	tuple->bsize = data_len;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);
	tuple->data_offset = sizeof(struct tuple);
	char *raw = (char *) tuple + tuple->data_offset;
	memcpy(raw, data, data_len);
	say_debug("%s(%zu) = %p", __func__, data_len, tuple);
	return tuple;
}

static void
runtime_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	size_t total = sizeof(struct tuple) + tuple->bsize;
	tuple_format_ref(format, -1);
#if !defined(NDEBUG)
	memset(tuple, '#', total);
#endif
	(void) total;
	free(tuple);
}

struct tuple_format_vtab runtime_tuple_format_vtab = {
	runtime_tuple_delete,
};

int
tuple_validate_raw(struct tuple_format *format, const char *tuple)
{
	if (format->field_count == 0)
		return 0; /* Nothing to check */

	/* Check to see if the tuple has a sufficient number of fields. */
	uint32_t field_count = mp_decode_array(&tuple);
	if (format->exact_field_count > 0 &&
	    format->exact_field_count != field_count) {
		diag_set(ClientError, ER_EXACT_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->exact_field_count);
		return -1;
	}
	if (unlikely(field_count < format->field_count)) {
		diag_set(ClientError, ER_INDEX_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->field_count);
		return -1;
	}

	/* Check field types */
	for (uint32_t i = 0; i < format->field_count; i++) {
		if (key_mp_type_validate(format->fields[i].type,
					 mp_typeof(*tuple), ER_FIELD_TYPE,
					 i + TUPLE_INDEX_BASE))
			return -1;
		mp_next(&tuple);
	}
	return 0;
}

/**
 * Incremented on every snapshot and is used to distinguish tuples
 * which were created after start of a snapshot (these tuples can
 * be freed right away, since they are not used for snapshot) or
 * before start of a snapshot (these tuples can be freed only
 * after the snapshot has finished, otherwise it'll write bad data
 * to the snapshot file).
 */

const char *
tuple_seek(struct tuple_iterator *it, uint32_t fieldno)
{
	const char *field = tuple_field(it->tuple, fieldno);
	if (likely(field != NULL)) {
		it->pos = field;
		it->fieldno = fieldno;
		return tuple_next(it);
	} else {
		it->pos = it->end;
		it->fieldno = tuple_field_count(it->tuple);
		return NULL;
	}
}

const char *
tuple_next(struct tuple_iterator *it)
{
	if (it->pos < it->end) {
		const char *field = it->pos;
		mp_next(&it->pos);
		assert(it->pos <= it->end);
		it->fieldno++;
		return field;
	}
	return NULL;
}

char *
tuple_extract_key(const struct tuple *tuple, const struct key_def *key_def,
		  uint32_t *key_size)
{
	const char *data = tuple_data(tuple);
	uint32_t part_count = key_def->part_count;
	uint32_t bsize = mp_sizeof_array(part_count);
	const struct tuple_format *format = tuple_format(tuple);
	const uint32_t *field_map = tuple_field_map(tuple);

	/* Calculate key size. */
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field =
			tuple_field_raw(format, data, field_map,
					key_def->parts[i].fieldno);
		const char *end = field;
		mp_next(&end);
		bsize += end - field;
	}

	char *key = (char *) region_alloc(&fiber()->gc, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region", "tuple_extract_key");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, part_count);
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field =
			tuple_field_raw(format, data, field_map,
					key_def->parts[i].fieldno);
		const char *end = field;
		mp_next(&end);
		bsize = end - field;
		memcpy(key_buf, field, bsize);
		key_buf += bsize;
	}
	if (key_size != NULL)
		*key_size = key_buf - key;
	return key;
}

char *
tuple_extract_key_raw(const char *data, const char *data_end,
		      const struct key_def *key_def, uint32_t *key_size)
{
	/* allocate buffer with maximal possible size */
	char *key = (char *) region_alloc(&fiber()->gc, data_end - data);
	if (key == NULL) {
		diag_set(OutOfMemory, data_end - data, "region",
			 "tuple_extract_key_raw");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	const char *field0 = data;
	mp_decode_array(&field0);
	const char *field0_end = field0;
	mp_next(&field0_end);
	const char *field = field0;
	const char *field_end = field0_end;
	uint32_t current_fieldno = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		uint32_t fieldno = key_def->parts[i].fieldno;
		if (fieldno < current_fieldno) {
			/* Rewind. */
			field = field0;
			field_end = field0_end;
			current_fieldno = 0;
		}
		while (current_fieldno < fieldno) {
			field = field_end;
			mp_next(&field_end);
			current_fieldno++;
		}
		memcpy(key_buf, field, field_end - field);
		key_buf += field_end - field;
		assert(key_buf - key <= data_end - data);
	}
	if (key_size != NULL)
		*key_size = (uint32_t)(key_buf - key);
	return key;
}

int
tuple_init(void)
{
	if (tuple_format_init() != 0)
		return -1;

	/*
	 * Create a format for runtime tuples
	 */
	RLIST_HEAD(empty_list);
	tuple_format_runtime = tuple_format_new(&empty_list,
						&runtime_tuple_format_vtab);
	if (tuple_format_runtime == NULL) {
		tuple_format_free();
		return -1;
	}
	/* Make sure this one stays around. */
	tuple_format_ref(tuple_format_runtime, 1);

	mempool_create(&tuple_iterator_pool, &cord()->slabc,
		       sizeof(struct tuple_iterator));

	box_tuple_last = NULL;

	return 0;
}

void
tuple_free(void)
{
	/* Unref last tuple returned by public C API */
	if (box_tuple_last != NULL) {
		tuple_unref(box_tuple_last);
		box_tuple_last = NULL;
	}

	mempool_destroy(&tuple_iterator_pool);

	tuple_format_ref(tuple_format_runtime, -1);
	tuple_format_free();
}

box_tuple_format_t *
box_tuple_format_default(void)
{
	return tuple_format_runtime;
}

int
box_tuple_ref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_ref(tuple);
}

void
box_tuple_unref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_unref(tuple);
}

uint32_t
box_tuple_field_count(const box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_field_count(tuple);
}

size_t
box_tuple_bsize(const box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple->bsize;
}

ssize_t
box_tuple_to_buf(const box_tuple_t *tuple, char *buf, size_t size)
{
	assert(tuple != NULL);
	return tuple_to_buf(tuple, buf, size);
}

box_tuple_format_t *
box_tuple_format(const box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_format(tuple);
}

const char *
box_tuple_field(const box_tuple_t *tuple, uint32_t fieldno)
{
	assert(tuple != NULL);
	return tuple_field(tuple, fieldno);
}

typedef struct tuple_iterator box_tuple_iterator_t;

box_tuple_iterator_t *
box_tuple_iterator(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	struct tuple_iterator *it = (struct tuple_iterator *)
		mempool_alloc(&tuple_iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, tuple_iterator_pool.objsize,
			 "mempool", "new slab");
	}
	if (tuple_ref(tuple) != 0) {
		mempool_free(&tuple_iterator_pool, it);
		return NULL;
	}
	tuple_rewind(it, tuple);
	return it;
}

void
box_tuple_iterator_free(box_tuple_iterator_t *it)
{
	tuple_unref(it->tuple);
	mempool_free(&tuple_iterator_pool, it);
}

uint32_t
box_tuple_position(box_tuple_iterator_t *it)
{
	return it->fieldno;
}

void
box_tuple_rewind(box_tuple_iterator_t *it)
{
	tuple_rewind(it, it->tuple);
}

const char *
box_tuple_seek(box_tuple_iterator_t *it, uint32_t fieldno)
{
	return tuple_seek(it, fieldno);
}

const char *
box_tuple_next(box_tuple_iterator_t *it)
{
	return tuple_next(it);
}
