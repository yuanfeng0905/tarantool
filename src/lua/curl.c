/*
 * Copyright (C) 2016-2017 Tarantool AUTHORS: please see AUTHORS file.
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

#ifndef CURL_LUA_C_INCLUDED
#define CURL_LUA_C_INCLUDED 1

#define DRIVER_LUA_UDATA_NAME	"__tnt_curl"
#define TNT_CURL_VERSION_MAJOR 2
#define TNT_CURL_VERSION_MINOR 2
#define TNT_CURL_VERSION_PATCH 7

#include "src/curl.h"
#include "curl.h"
#include "say.h"
#include "lua/utils.h"


static
int
curl_make_result(lua_State *L, struct request_t* r);

static inline
struct lib_ctx_t*
ctx_get(lua_State *L)
{
  return (struct lib_ctx_t *)
      luaL_checkudata(L, 1, DRIVER_LUA_UDATA_NAME);
}

static inline
void
add_field_u64(lua_State *L, const char *key, uint64_t value)
{
    lua_pushstring(L, key);
    lua_pushinteger(L, value);
    lua_settable(L, -3);  /* 3rd element from the stack top */
}

/** lib Lua API {{{
 */

static
int
async_request(lua_State *L)
{
    const char *reason = "unknown error";
    struct lib_ctx_t *ctx = ctx_get(L);
    if (ctx == NULL)
        return luaL_error(L, "can't get lib ctx");
    ctx->done = false;
    if (ctx->done)
        return luaL_error(L, "curl stopped");

    struct request_start_args_t req_args;
    request_start_args_init(&req_args);

    const char *method = luaL_checkstring(L, 2);
    const char *url    = luaL_checkstring(L, 3);
    struct header* hh = NULL;
    /** Set Options {{{
     */
    if (lua_istable(L, 4)) {

        const int top = lua_gettop(L);

        lua_pushstring(L, "body");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.body = lua_tostring(L, top + 1);
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

            hh = (struct header*) malloc(sizeof(struct header) * (size_hh + 1));
            if (!hh) {
                reason = "Can't allocate memory for headers passed from Lua";
                goto error_exit;
            }

            size_t i = 0;
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                hh[i].key = lua_tostring(L, -2);
                hh[i++].value = lua_tostring(L, -1);
                lua_pop(L, 1);
            } // while
            hh[i].key = NULL;
        }
        lua_pop(L, 1);
        req_args.headers = hh;

        /* SSL/TLS cert  {{{ */
        lua_pushstring(L, "ca_path");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.ca_path = lua_tostring(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "ca_file");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.ca_file = lua_tostring(L, top + 1);
        lua_pop(L, 1);
        /* }}} */

        lua_pushstring(L, "max_conns");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.max_conns = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "keepalive_idle");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.keepalive_idle = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "keepalive_interval");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.keepalive_interval = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "low_speed_limit");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.low_speed_limit = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "low_speed_time");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.low_speed_time = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "read_timeout");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.read_timeout = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "connect_timeout");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.connect_timeout = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        lua_pushstring(L, "dns_cache_timeout");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1))
            req_args.dns_cache_timeout = (long) lua_tointeger(L, top + 1);
        lua_pop(L, 1);

        /* Debug- / Internal- options */
        lua_pushstring(L, "curl_verbose");
        lua_gettable(L, 4);
        if (!lua_isnil(L, top + 1) && lua_isboolean(L, top + 1))
            req_args.curl_verbose = true;
        lua_pop(L, 1);
    } else {
        reason = "4-arg have to be a table";
        goto error_exit;
    }
    /* }}} */
    

    /* Note that the add_handle() will set a
     * time-out to trigger very soon so that
     * the necessary socket_action() call will be
     * called by this app */
    struct request_t* r = http_request(ctx, method, url, &req_args);
 
    if (hh)
        free(hh);

    if (!r) {
        reason = "Error in request";
        goto error_exit;
    }
    int res = curl_make_result(L, r);
    free_request(ctx, r);
    return  res;

error_exit:
    return luaL_error(L, reason);
}

static
int
get_stat(lua_State *L)
{
    struct lib_ctx_t *ctx = ctx_get(L);
    if (ctx == NULL)
        return luaL_error(L, "can't get lib ctx");

    struct curl_ctx_t *l = ctx->curl_ctx;
    if (l == NULL)
        return luaL_error(L, "it doesn't initialized");

    lua_newtable(L);

    add_field_u64(L, "active_requests", (uint64_t) l->stat.active_requests);
    add_field_u64(L, "sockets_added", (uint64_t) l->stat.sockets_added);
    add_field_u64(L, "sockets_deleted", (uint64_t) l->stat.sockets_deleted);
    add_field_u64(L, "total_requests", l->stat.total_requests);
    add_field_u64(L, "http_200_responses",  l->stat.http_200_responses);
    add_field_u64(L, "http_other_responses", l->stat.http_other_responses);
    add_field_u64(L, "failed_requests", (uint64_t) l->stat.failed_requests);

    return 1;
}

static 
int
curl_make_result(lua_State *L, struct request_t *r)
{
  lua_newtable(L);

  lua_pushstring(L, "curl_code");
  lua_pushinteger(L, r->response.curl_code);
  lua_settable(L, -3);   

  lua_pushstring(L, "http_code");
  lua_pushinteger(L, r->response.http_code);
  lua_settable(L, -3);

  lua_pushstring(L, "error_message");
  lua_pushstring(L, r->response.errmsg);
  lua_settable(L, -3);

  if (r->response.headers_buf.data) {
      lua_pushstring(L, "headers");
      lua_pushstring(L, r->response.headers_buf.data);
      lua_settable(L, -3);
  }

  if (r->response.body_buf.data) {
      lua_pushstring(L, "body");
      lua_pushstring(L, r->response.body_buf.data);
      lua_settable(L, -3);
  }

  return 1;
}

static inline
int
make_str_result(lua_State *L, bool ok, const char *str)
{
        lua_pushboolean(L, ok);
        lua_pushstring(L, str);
        return 2;
}

static inline
int
make_int_result(lua_State *L, bool ok, int i)
{
        lua_pushboolean(L, ok);
        lua_pushinteger(L, i);
        return 2;
}

static inline
int
make_errorno_result(lua_State *L, int the_errno)
{
        lua_pushboolean(L, false);
        lua_pushstring(L, strerror(the_errno));
        return 2;
}


static
int
pool_stat(lua_State *L)
{
    struct lib_ctx_t *ctx = ctx_get(L);
    if (ctx == NULL)
        return luaL_error(L, "can't get lib ctx");

    struct curl_ctx_t *l = ctx->curl_ctx;
    if (l == NULL)
        return luaL_error(L, "it doesn't initialized");

    lua_newtable(L);

    add_field_u64(L, "pool_size", (uint64_t) l->cpool.size);
    add_field_u64(L, "free", (uint64_t) request_pool_get_free_size(&l->cpool));

    return 1;
}



static
int
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


static
int
new(lua_State *L)
{

    struct lib_ctx_t *ctx = (struct lib_ctx_t *)
            lua_newuserdata(L, sizeof(struct lib_ctx_t));
    if (ctx == NULL)
        return luaL_error(L, "lua_newuserdata failed: lib_ctx_t");

    ctx->curl_ctx = NULL;
    ctx->done    = false;

    struct curl_args_t args = { .pipeline = false,
                         .max_conns = 5,
                         .pool_size = 10000,
                         .buffer_size = 2048
                        };

    /* pipeline: 1 - on, 0 - off */
    args.pipeline  = (bool) luaL_checkint(L, 1);
    args.max_conns = luaL_checklong(L, 2);
    args.pool_size = (size_t) luaL_checklong(L, 3);
    args.buffer_size = (size_t) luaL_checklong(L, 4);
    ctx->curl_ctx = curl_ctx_new(&args);
    if (ctx->curl_ctx == NULL)
        return luaL_error(L, "curl_new failed");


    luaL_getmetatable(L, DRIVER_LUA_UDATA_NAME);
    lua_setmetatable(L, -2);

    return 1;
}



static
int
cleanup(lua_State *L)
{
    curl_delete(ctx_get(L));

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
    {"new",     new},
    {NULL,      NULL}
};

static const struct luaL_Reg M[] = {
    {"async_request", async_request},
    {"stat",          get_stat},
    {"pool_stat",     pool_stat},
    {"free",          cleanup},
    {NULL,            NULL}
};

/*
 * Lib initializer
 */
LUA_API
int
luaopen_curl_driver(lua_State *L)
{
	luaL_register_type(L, DRIVER_LUA_UDATA_NAME, M);
	luaL_register(L, "curl.driver", R);
	return 1;
}

#endif /* CURL_LUA_C_INCLUDED */
