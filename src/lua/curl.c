/*
 * Copyright (C) 2016-2017 Tarantool AUTHORS: please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the
 *	  following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#ifndef CURL_LUA_C_INCLUDED
#define CURL_LUA_C_INCLUDED 1

/**
 * Unique name for userdata metatables
 */
#define DRIVER_LUA_UDATA_NAME	"__tnt_curl"
#define TNT_CURL_VERSION_MAJOR 2
#define TNT_CURL_VERSION_MINOR 2
#define TNT_CURL_VERSION_PATCH 7

#include "src/curl.h"
#include "curl.h"
#include "say.h"
#include "lua/utils.h"


static int
curl_make_result(lua_State *L, struct curl_response *r);

static inline struct curl_ctx*
ctx_get(lua_State *L)
{
	return (struct curl_ctx *)
			luaL_checkudata(L, 1, DRIVER_LUA_UDATA_NAME);
}

static inline void
add_field_u64(lua_State *L, const char *key, uint64_t value)
{
	lua_pushstring(L, key);
	lua_pushinteger(L, value);
	lua_settable(L, -3);
}

/** lib Lua API {{{
 */

#define BUFFER_SIZE 2048

static int
luaT_curl_request(lua_State *L)
{
	const char *reason = "unknown error";
	struct curl_ctx *ctx = ctx_get(L);
	if (ctx == NULL)
		return luaL_error(L, "can't get lib ctx");

	ctx->done = false;
	if (ctx->done)
		return luaL_error(L, "curl stopped");

	struct curl_request *req = curl_request_new(ctx, BUFFER_SIZE);
	if (!req) {
		return luaL_error(L, "can't get new request");
	}

	const char *method = luaL_checkstring(L, 2);
	curl_set_method(req, method);

	const char *url    = luaL_checkstring(L, 3);
	curl_set_url(req, url);

	struct curl_header *hh = NULL;
	/** Set Options {{{
	 */
	if (lua_istable(L, 4)) {

		const int top = lua_gettop(L);

		lua_pushstring(L, "body");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_body(req, lua_tostring(L, top + 1));
		lua_pop(L, 1);

		/** Http headers */
		lua_pushstring(L, "headers");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1)) {
			size_t size_hh = 0;
			/* need to know the size of table*/
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				size_hh++;
				lua_pop(L, 1);
			}

			if (size_hh > 0) {
				hh = (struct curl_header *)
					malloc(sizeof(struct curl_header) *
							(size_hh + 1));
				if (!hh) {
					reason = "Can't allocate memory\
						for headers passed from Lua";
					goto error_exit;
				}

				size_t i = 0;
				lua_pushnil(L);
				while (lua_next(L, -2) != 0) {
					hh[i].key = lua_tostring(L, -2);
					hh[i++].value = lua_tostring(L, -1);
					lua_pop(L, 1);
				}
				hh[i].key = NULL;
			}
			if (curl_set_headers(req, hh) < 0) {
				reason = "can't allocate memory\
				  	(curl_set_header)";
			}
		}
		lua_pop(L, 1);


		lua_pushstring(L, "ca_path");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_ca_path(req,
					lua_tostring(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "ca_file");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_ca_file(req,
					lua_tostring(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "max_conns");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_max_conns(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "keepalive_idle");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_keepalive_idle(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "keepalive_interval");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_keepalive_interval(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "low_speed_limit");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_low_speed_limit(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "low_speed_time");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_low_speed_time(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "read_timeout");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_read_timeout(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "connect_timeout");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_connect_timeout(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		lua_pushstring(L, "dns_cache_timeout");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1))
			curl_set_dns_cache_timeout(req,
					(long) lua_tointeger(L, top + 1));
		lua_pop(L, 1);

		/* Debug- / Internal- options */
		lua_pushstring(L, "curl_verbose");
		lua_gettable(L, 4);
		if (!lua_isnil(L, top + 1) && lua_isboolean(L, top + 1))
			curl_set_verbose(req, true);
		lua_pop(L, 1);
	} else {
		reason = "fourth argument have to be a table";
		goto error_exit;
	}
	/* }}} */

	struct curl_response *resp =
		curl_request_execute(req);

	if (hh)
		free(hh);

	if (!resp) {
		reason = "Error in request";
		goto error_exit;
	}

	int result = curl_make_result(L, resp);
	curl_request_delete(req);
	curl_response_destroy(ctx, resp);
	return	result;

error_exit:
	if (req)
		curl_request_delete(req);
	return luaL_error(L, reason);
}

static int
get_stat(lua_State *L)
{
	struct curl_ctx *ctx = ctx_get(L);
	if (ctx == NULL)
		return luaL_error(L, "can't get curl ctx");

	lua_newtable(L);

	add_field_u64(L, "active_requests", (uint64_t) ctx->stat.active_requests);
	add_field_u64(L, "sockets_added", (uint64_t) ctx->stat.sockets_added);
	add_field_u64(L, "sockets_deleted", (uint64_t) ctx->stat.sockets_deleted);
	add_field_u64(L, "total_requests", ctx->stat.total_requests);
	add_field_u64(L, "http_200_responses",	ctx->stat.http_200_responses);
	add_field_u64(L, "http_other_responses", ctx->stat.http_other_responses);
	add_field_u64(L, "failed_requests", (uint64_t) ctx->stat.failed_requests);

	return 1;
}

static int
curl_make_result(lua_State *L, struct curl_response *resp)
{
	assert(resp);
	lua_newtable(L);

	lua_pushstring(L, "curl_code");
	lua_pushinteger(L, resp->curl_code);
	lua_settable(L, -3);

	lua_pushstring(L, "http_code");
	lua_pushinteger(L, resp->http_code);
	lua_settable(L, -3);

	lua_pushstring(L, "error_message");
	lua_pushstring(L, resp->errmsg);
	lua_settable(L, -3);

	if (resp->headers) {
		lua_pushstring(L, "headers");
		lua_pushstring(L, resp->headers);
		lua_settable(L, -3);
	}

	if (resp->body) {
		lua_pushstring(L, "body");
		lua_pushstring(L, resp->body);
		lua_settable(L, -3);
	}

	return 1;
}

static inline int
make_str_result(lua_State *L, bool ok, const char *str)
{
		lua_pushboolean(L, ok);
		lua_pushstring(L, str);
		return 2;
}

static inline int
make_int_result(lua_State *L, bool ok, int i)
{
		lua_pushboolean(L, ok);
		lua_pushinteger(L, i);
		return 2;
}


static int
version(lua_State *L)
{
	char version[sizeof("tarantool.curl: xxx.xxx.xxx") +
	      		   sizeof("curl: xxx.xxx.xxx,") +
	      		   sizeof("libev: xxx.xxx") ];
	      say(S_INFO, "%s", "hello");
	snprintf(version, sizeof(version) - 1,
	      		"tarantool.curl: %i.%i.%i, curl: %i.%i.%i, libev: %i.%i",
	      		TNT_CURL_VERSION_MAJOR,
	      		TNT_CURL_VERSION_MINOR,
	      		TNT_CURL_VERSION_PATCH,

	      		LIBCURL_VERSION_MAJOR,
	      		LIBCURL_VERSION_MINOR,
	      		LIBCURL_VERSION_PATCH,

	      		EV_VERSION_MAJOR,
	      		EV_VERSION_MINOR );

	return make_str_result(L, true, version);
}


static int
new(lua_State *L)
{

	struct curl_ctx *ctx = (struct curl_ctx *)
			lua_newuserdata(L, sizeof(struct curl_ctx));
	if (ctx == NULL)
		return luaL_error(L, "lua_newuserdata failed: curl_ctx");

	ctx->done = false;

	/* pipeline: 1 - on, 0 - off */
	bool pipeline  = (bool) luaL_checkint(L, 1);
	long max_conns = luaL_checklong(L, 2);
	ctx = curl_ctx_create(ctx, pipeline, max_conns);
	if (ctx == NULL)
		return luaL_error(L, "curl_create failed");

	luaL_getmetatable(L, DRIVER_LUA_UDATA_NAME);
	lua_setmetatable(L, -2);

	return 1;
}

static int
cleanup(lua_State *L)
{
	curl_ctx_destroy(ctx_get(L));

	/* remove all methods operating on ctx */
	lua_newtable(L);
	lua_setmetatable(L, -2);

	return make_int_result(L, true, 0);
}

/*
 * }}}
 */

/*
 * Lists of exporting: object and/or functions to the Lua
 */

static const struct luaL_Reg R[] = {
	{"version", version},
	{"new",		new},
	{NULL,		NULL}
};

static const struct luaL_Reg M[] = {
	{"luaT_curl_request",	luaT_curl_request},
	{"stat",		get_stat},
	{"free",		cleanup},
	{"__gc",		cleanup},
	{NULL,			  NULL}
};

/*
 * Lib initializer
 */
LUA_API int
luaopen_curl_driver(lua_State *L)
{
	luaL_register_type(L, DRIVER_LUA_UDATA_NAME, M);
	luaL_register(L, "curl.driver", R);
	return 1;
}

#endif /* CURL_LUA_C_INCLUDED */
