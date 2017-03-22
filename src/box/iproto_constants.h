#ifndef TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
#define TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
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
#include <stdbool.h>
#include <stdint.h>
#include <trivia/util.h>

#include <msgpuck.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	/** Maximal iproto package body length (2GiB) */
	IPROTO_BODY_LEN_MAX = 2147483648UL,
	/* Maximal length of text handshake (greeting) */
	IPROTO_GREETING_SIZE = 128,
	/** marker + len + prev crc32 + cur crc32 + (padding) */
	XLOG_FIXHEADER_SIZE = 19
};

enum iproto_key {
	IPROTO_REQUEST_TYPE = 0x00,
	IPROTO_SYNC = 0x01,
	/* Replication keys (header) */
	IPROTO_REPLICA_ID = 0x02,
	IPROTO_LSN = 0x03,
	IPROTO_TIMESTAMP = 0x04,
	IPROTO_SCHEMA_ID = 0x05,
	/* Leave a gap for other keys in the header. */
	IPROTO_SPACE_ID = 0x10,
	IPROTO_INDEX_ID = 0x11,
	IPROTO_LIMIT = 0x12,
	IPROTO_OFFSET = 0x13,
	IPROTO_ITERATOR = 0x14,
	IPROTO_INDEX_BASE = 0x15,
	/* Leave a gap between integer values and other keys */
	IPROTO_KEY = 0x20,
	IPROTO_TUPLE = 0x21,
	IPROTO_FUNCTION_NAME = 0x22,
	IPROTO_USER_NAME = 0x23,
	/* Replication keys (body) */
	IPROTO_INSTANCE_UUID = 0x24,
	IPROTO_CLUSTER_UUID = 0x25,
	IPROTO_VCLOCK = 0x26,
	IPROTO_EXPR = 0x27, /* EVAL */
	IPROTO_OPS = 0x28, /* UPSERT but not UPDATE ops, because of legacy */
	/* Leave a gap between request keys and response keys */
	IPROTO_DATA = 0x30,
	IPROTO_ERROR = 0x31,
	IPROTO_KEY_MAX
};

#define bit(c) (1ULL<<IPROTO_##c)

#define IPROTO_HEAD_BMAP (bit(REQUEST_TYPE) | bit(SYNC) | bit(REPLICA_ID) |\
			  bit(LSN) | bit(SCHEMA_ID))
#define IPROTO_BODY_BMAP (bit(SPACE_ID) | bit(INDEX_ID) | bit(LIMIT) |\
			  bit(OFFSET) | bit(ITERATOR) | bit(INDEX_BASE) |\
			  bit(KEY) | bit(TUPLE) | bit(FUNCTION_NAME) | \
			  bit(USER_NAME) | bit(EXPR) | bit(OPS))

static inline bool
xrow_header_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_HEAD_BMAP & (1ULL<<key);
}

static inline bool
iproto_body_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_BODY_BMAP & (1ULL<<key);
}

#undef bit

static inline uint64_t
iproto_key_bit(unsigned char key)
{
	return 1ULL << key;
}

extern const unsigned char iproto_key_type[IPROTO_KEY_MAX];

/**
 * IPROTO command codes
 */
enum iproto_type {
	/* command is successful */
	IPROTO_OK = 0,
	/* dml command codes (see extra dml command codes) */
	IPROTO_SELECT = 1,
	IPROTO_INSERT = 2,
	IPROTO_REPLACE = 3,
	IPROTO_UPDATE = 4,
	IPROTO_DELETE = 5,
	IPROTO_CALL_16 = 6,
	IPROTO_AUTH = 7,
	IPROTO_EVAL = 8,
	IPROTO_UPSERT = 9,
	IPROTO_CALL = 10,
	IPROTO_TYPE_STAT_MAX = IPROTO_CALL + 1,
	/* admin command codes */
	IPROTO_PING = 64,
	IPROTO_JOIN = 65,
	IPROTO_SUBSCRIBE = 66,
	IPROTO_TYPE_ADMIN_MAX = IPROTO_SUBSCRIBE + 1,

	VY_META_RUN_INFO = 760,
	VY_META_PAGE_INFO = 761,
	VY_META_PAGE_INDEX = 762,
	VY_XCTL_CREATE_INDEX = 763,
	VY_XCTL_DROP_INDEX = 764,
	VY_XCTL_INSERT_RANGE = 765,
	VY_XCTL_DELETE_RANGE = 766,
	VY_XCTL_PREPARE_RUN = 767,
	VY_XCTL_INSERT_RUN = 768,
	VY_XCTL_DELETE_RUN = 769,
	VY_XCTL_FORGET_RUN = 770,

	/* command failed = (IPROTO_TYPE_ERROR | ER_XXX from errcode.h) */
	IPROTO_TYPE_ERROR = 1 << 15
};

extern const char *iproto_type_strs[];
/** Key names. */
extern const char *iproto_key_strs[];
/** A map of mandatory members of an iproto DML request. */
extern const uint64_t iproto_body_key_map[];

static inline const char *
iproto_type_name(uint32_t type)
{
	if (type >= IPROTO_TYPE_STAT_MAX)
		return "unknown";
	return iproto_type_strs[type];
}

/**
 * A read only request, CALL is included since it
 * may be read-only, and there are separate checks
 * for all database requests issues from CALL.
 */
static inline bool
iproto_type_is_select(uint32_t type)
{
	return type <= IPROTO_SELECT || type == IPROTO_CALL || type == IPROTO_EVAL;
}

/** A common request with a mandatory and simple body (key, tuple, ops)  */
static inline bool
iproto_type_is_request(uint32_t type)
{
	return type > IPROTO_OK && type <= IPROTO_UPSERT;
}

/**
 * The request is "synchronous": no other requests
 * on this connection should be taken before this one
 * ends.
 */
static inline bool
iproto_type_is_sync(uint32_t type)
{
	return type == IPROTO_JOIN || type == IPROTO_SUBSCRIBE;
}

/** A data manipulation request. */
static inline bool
iproto_type_is_dml(uint32_t type)
{
	return (type >= IPROTO_SELECT && type <= IPROTO_DELETE) ||
		type == IPROTO_UPSERT;
}

/** This is an error. */
static inline bool
iproto_type_is_error(uint32_t type)
{
	return (type & IPROTO_TYPE_ERROR) != 0;
}

/** The snapshot row metadata repeats the structure of REPLACE request. */
struct PACKED request_replace_body {
	uint8_t m_body;
	uint8_t k_space_id;
	uint8_t m_space_id;
	uint32_t v_space_id;
	uint8_t k_tuple;
};

/*
 * Vinyl keys
 */
enum vy_request_run_key {
	VY_RUN_MIN_LSN = 1,
	VY_RUN_MAX_LSN = 2,
	VY_RUN_PAGE_COUNT = 3,
	VY_RUN_BLOOM = 4,
	VY_RUN_KEY_MAX = VY_RUN_BLOOM + 1
};

extern const char *vy_run_info_key_strs[VY_RUN_KEY_MAX];

enum vy_request_page_key {
	VY_PAGE_OFFSET = 1,
	VY_PAGE_SIZE = 2,
	VY_PAGE_REQUEST_COUNT = 3,
	VY_PAGE_MIN_KEY = 4,
	VY_PAGE_DATA_SIZE = 5,
	VY_PAGE_INDEX_OFFSET = 6,
	VY_PAGE_INDEX = 7,
	VY_PAGE_KEY_MAX = VY_PAGE_INDEX_OFFSET + 1
};

extern const char *vy_page_info_key_strs[VY_PAGE_KEY_MAX];

/**
 * Integer key of a field in the xctl_record structure.
 * Used for packing a record in MsgPack.
 */
enum xctl_key {
	XCTL_KEY_VY_INDEX_ID		= 0,
	XCTL_KEY_VY_RANGE_ID		= 1,
	XCTL_KEY_VY_RUN_ID		= 2,
	XCTL_KEY_VY_RANGE_BEGIN		= 3,
	XCTL_KEY_VY_RANGE_END		= 4,
	XCTL_KEY_IID			= 5,
	XCTL_KEY_SPACE_ID		= 6,
	XCTL_KEY_PATH			= 7,
	XCTL_KEY_MAX			= XCTL_KEY_PATH + 1
};

/** xctl_key -> human readable name. */
extern const char *xctl_key_name[];

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED */
