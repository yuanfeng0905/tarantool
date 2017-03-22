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

#include "xlog.h"

#include <say.h>
#include <diag.h>
#include <msgpuck/msgpuck.h>

#include <box/xlog.h>
#include <box/xrow.h>
#include <box/iproto_constants.h>
#include <box/tuple.h>
#include <box/lua/tuple.h>
#include <lua/msgpack.h>
#include <lua/utils.h>
#include "box/memtx_tuple.h"

/* {{{ Helpers */

static uint32_t CTID_STRUCT_XLOG_CURSOR_REF = 0;
static const char *xloglib_name = "xlog";

static int
lbox_pushcursor(struct lua_State *L, struct xlog_cursor *cur)
{
	struct xlog_cursor **pcur = NULL;
	pcur = (struct xlog_cursor **)luaL_pushcdata(L,
			CTID_STRUCT_XLOG_CURSOR_REF);
	*pcur = cur;
	return 1;
}

static struct xlog_cursor *
lbox_checkcursor(struct lua_State *L, int narg, const char *src)
{
	uint32_t ctypeid;
	void *data = NULL;
	data = (struct xlog_cursor *)luaL_checkcdata(L, narg, &ctypeid);
	assert(ctypeid == CTID_STRUCT_XLOG_CURSOR_REF);
	if (ctypeid != (uint32_t )CTID_STRUCT_XLOG_CURSOR_REF)
		luaL_error(L, "%s: expecting xlog_cursor object", src);
	return *(struct xlog_cursor **)data;
}

/* }}} */

/* {{{ Xlog Parser */

static void
lbox_xlog_parse_body_kv(struct lua_State *L, int type, const char **beg, const char *end)
{
	if (mp_typeof(**beg) != MP_UINT)
		luaL_error(L, "Broken type of body key");
	uint32_t v = mp_decode_uint(beg);
	if (type < IPROTO_TYPE_STAT_MAX && v < IPROTO_KEY_MAX &&
	    iproto_key_strs[v] != NULL &&
	    iproto_key_strs[v][0] > 0) {
		lua_pushstring(L, iproto_key_strs[v]);
	} else if (type == VY_META_RUN_INFO && v < VY_RUN_KEY_MAX) {
		lua_pushstring(L, vy_run_info_key_strs[v]);
	} else if (type == VY_META_PAGE_INFO && v < VY_PAGE_KEY_MAX) {
		lua_pushstring(L, vy_page_info_key_strs[v]);
	} else if (type >= VY_XCTL_CREATE_INDEX && type <= VY_XCTL_FORGET_RUN &&
		   v < XCTL_KEY_MAX) {
		lua_pushstring(L, xctl_key_name[v]);
	} else {
		lua_pushinteger(L, v); /* unknown key */
	}

	box_tuple_format_t *fmt = NULL;
	if ((v == IPROTO_KEY || v == IPROTO_TUPLE) &&
	    (mp_typeof(**beg) == MP_ARRAY) &&
	    (fmt = box_tuple_format_default()) != NULL) {
		/*
		 * Push tuple if possible.
		 *
		 * Sic: box_tuple_format_default() != NULL is used to check
		 * that box is initialized.
		 */
		const char *tuple_beg = *beg;
		mp_next(beg);
		struct tuple *tuple = NULL;
		assert(fmt != NULL);
		tuple = box_tuple_new(fmt, tuple_beg, *beg);
		if (tuple == NULL)
			luaT_error(L);
		luaT_pushtuple(L, tuple);
	} else {
		/*
		 * Push Lua objects
		 */
		const char *tmp = *beg;
		if (mp_check(&tmp, end) != 0) {
			lua_pushstring(L, "<invalid msgpack>");
		} else {
			luamp_decode(L, luaL_msgpack_default, beg);
		}
	}
	lua_settable(L, -3);
}

static int
lbox_xlog_parse_body(struct lua_State *L, int type, const char *ptr, size_t len)
{
	const char **beg = &ptr;
	const char *end = ptr + len;
	if (mp_typeof(**beg) != MP_MAP)
		return -1;
	uint32_t size = mp_decode_map(beg);
	uint32_t i;
	for (i = 0; i < size && *beg < end; i++)
		lbox_xlog_parse_body_kv(L, type, beg, end);
	if (i != size)
		say_warn("warning: decoded %u values from"
			 " MP_MAP, %u expected", i, size);
	return 0;
}

static int
lbox_xlog_parser_iterate(struct lua_State *L)
{
	struct xlog_cursor *cur = lbox_checkcursor(L, 1, "xlog:pairs()");

	struct xrow_header row;
	int rc = 0;
	/* skip all bad read requests */
	while (true) {
		rc = xlog_cursor_next_row(cur, &row);
		if (rc == 0)
			break;
		if (rc < 0) {
			struct error *e = diag_last_error(diag_get());
			if (e->type != &type_XlogError)
				luaT_error(L);
		}
		while ((rc = xlog_cursor_next_tx(cur)) < 0) {
			struct error *e = diag_last_error(diag_get());
			if (e->type != &type_XlogError)
				luaT_error(L);
			if ((rc = xlog_cursor_find_tx_magic(cur)) < 1)
				luaT_error(L);
		}
		if (rc == 1)
			break;
	}
	if (rc == 1)
		return 0; /* EOF */
	assert(rc == 0);

	lua_pushinteger(L, row.lsn);
	lua_newtable(L);
	lua_pushstring(L, "HEADER");

	lua_newtable(L);
	lua_pushstring(L, "type");
	if (row.type < IPROTO_TYPE_STAT_MAX &&
	    iproto_type_strs[row.type] != NULL) {
		lua_pushstring(L, iproto_type_strs[row.type]);
	} else {
		lua_pushnumber(L, row.type); /* unknown key */
	}
	lua_settable(L, -3); /* type */
	lua_pushstring(L, "lsn");
	lua_pushinteger(L, row.lsn);
	lua_settable(L, -3); /* lsn */
	lua_pushstring(L, "replica_id");
	lua_pushinteger(L, row.replica_id);
	lua_settable(L, -3); /* replica_id */
	lua_pushstring(L, "timestamp");
	lua_pushnumber(L, row.tm);
	lua_settable(L, -3); /* timestamp */

	lua_settable(L, -3); /* HEADER */

	assert(row.bodycnt == 1); /* always 1 for read */
	lua_pushstring(L, "BODY");
	lua_newtable(L);
	lbox_xlog_parse_body(L, row.type, (char *)row.body[0].iov_base,
			     row.body[0].iov_len);
	lua_settable(L, -3);  /* BODY */

	return 2;
}

/* }}} */

static void
lbox_xlog_parser_close(struct xlog_cursor *cur) {
	if (cur == NULL)
		return;
	xlog_cursor_close(cur, false);
	free(cur);
}

static int
lbox_xlog_parser_gc(struct lua_State *L)
{
	struct xlog_cursor *cur = lbox_checkcursor(L, 1, "xlog:gc()");
	lbox_xlog_parser_close(cur);
	return 0;
}

static int
lbox_xlog_parser_open_pairs(struct lua_State *L)
{
	int args_n = lua_gettop(L);
	if (args_n != 1 || !lua_isstring(L, 1))
		luaL_error(L, "Usage: parser.open(log_filename)");

	const char *filename = luaL_checkstring(L, 1);

	/* Construct xlog cursor */
	struct xlog_cursor *cur = (struct xlog_cursor *)calloc(1,
			sizeof(struct xlog_cursor));
	if (cur == NULL) {
		diag_set(OutOfMemory, sizeof(struct xlog_cursor),
			 "malloc", "struct xlog_cursor");
		return luaT_error(L);
	}
	/* Construct xlog object */
	if (xlog_cursor_open(cur, filename) < 0) {
		return luaT_error(L);
	}
	if (strncmp(cur->meta.filetype, "SNAP", 4) != 0 &&
	    strncmp(cur->meta.filetype, "XLOG", 4) != 0 &&
	    strncmp(cur->meta.filetype, "RUN", 3) != 0 &&
	    strncmp(cur->meta.filetype, "INDEX", 5) != 0 &&
	    strncmp(cur->meta.filetype, "DATA", 4) != 0 &&
	    strncmp(cur->meta.filetype, "XCTL", 4) != 0) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "'%.*s' file type",
			 (int) strlen(cur->meta.filetype),
			 cur->meta.filetype);
		diag_set(ClientError, ER_UNSUPPORTED, "xlog reader", buf);
		xlog_cursor_close(cur, false);
		free(cur);
		return luaT_error(L);
	}
	/* push iteration function */
	lua_pushcclosure(L, &lbox_xlog_parser_iterate, 1);
	/* push log and set GC */
	lbox_pushcursor(L, cur);
	lua_pushcfunction(L, lbox_xlog_parser_gc);
	luaL_setcdatagc(L, -2);
	/* push iterator position */
	lua_pushinteger(L, 0);
	return 3;
}

static const struct luaL_reg lbox_xlog_parser_lib [] = {
	{ "pairs",	lbox_xlog_parser_open_pairs },
	{ NULL,		NULL                        }
};

void
box_lua_xlog_init(struct lua_State *L)
{
	int rc = 0;
	/* Get CTypeIDs */
	rc = luaL_cdef(L, "struct xlog_cursor;"); assert(rc == 0); (void) rc;
	CTID_STRUCT_XLOG_CURSOR_REF = luaL_ctypeid(L, "struct xlog_cursor&");
	assert(CTID_STRUCT_XLOG_CURSOR_REF != 0);

	luaL_register_module(L, xloglib_name, lbox_xlog_parser_lib);

	lua_newtable(L);
	lua_setmetatable(L, -2);
	lua_pop(L, 1);
}

/* }}} */
