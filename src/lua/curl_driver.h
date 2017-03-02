/*
 * Copyright (C) 2016 - 2017 Tarantool AUTHORS: please see AUTHORS file.
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

#ifndef DRIVER_H_INCLUDED
#define DRIVER_H_INCLUDED 1

#include "curl_wrapper.h"
#include "curl_utils.h"
#include "say.h"
/**
 * Unique name for userdata metatables
 */
#define DRIVER_LUA_UDATA_NAME	"__tnt_curl"
#define WORK_TIMEOUT 0.3
#define TNT_CURL_VERSION_MAJOR 2
#define TNT_CURL_VERSION_MINOR 2
#define TNT_CURL_VERSION_PATCH 7

typedef struct  {
    curl_ctx_t   *curl_ctx;
    bool         done;
} lib_ctx_t;


static inline
lib_ctx_t*
ctx_get(lua_State *L)
{
  return (lib_ctx_t *)
      luaL_checkudata(L, 1, DRIVER_LUA_UDATA_NAME);
}

static inline
int
curl_make_result(lua_State *L, CURLcode code, CURLMcode mcode, request_t *r)
{
  const char *emsg = NULL;
  if (code != CURL_LAST)
    emsg = curl_easy_strerror(code);
  else if (mcode != CURLM_LAST)
    emsg = curl_multi_strerror(mcode);

  lua_pushboolean(L, code != CURLE_OK);

  lua_newtable(L);

  lua_pushstring(L, "curl_code");
  lua_pushinteger(L, r->response.curl_code);
  lua_settable(L, -3);   
  lua_pushstring(L, "http_code");
  lua_pushinteger(L, r->response.http_code);
  lua_settable(L, -3);

  if (!emsg) {
    emsg = (r->response.errmsg != NULL) ? r->response.errmsg:"ok";   
  }

  lua_pushstring(L, "error_message");
  lua_pushstring(L, emsg);
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

  return 2;
}

static inline
void
add_field_u64(lua_State *L, const char *key, uint64_t value)
{
    lua_pushstring(L, key);
    lua_pushinteger(L, value);
    lua_settable(L, -3);  /* 3rd element from the stack top */
}

/**
 * @brief Lua/C API exports
 * @param L Lua stack
 * @return 1
 */
LUALIB_API int
luaopen_curl_driver(lua_State *L);


typedef struct {
    char* body;

} options;

typedef struct {

} response_t;

int curl_version();

lib_ctx_t *curl_new(bool pipeline, long max_conn, long pool_size, long buffer_size);

void curl_free(lib_ctx_t *ctx);

response_t *request(lib_ctx_t *ctx, const char* method, const char* url, const options_t* options);

#endif /* DRIVER_H_INCLUDED */
