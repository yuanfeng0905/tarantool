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

#include "curl_request_pool.h"

#include "curl_wrapper.h"

#include <string.h>
#include <assert.h>

static inline void reset_request(request_t *r);

static inline
bool
create_request(struct curl_ctx_s *ctx, size_t idx, size_t size_buf, request_t *r)
{
    assert(ctx);
    assert(r);

    memset(r, 0, sizeof(request_t));

    r->pool.idx = idx;
    r->curl_ctx = ctx;

    reset_request(r);

    r->response.headers_buf.data = (char*) malloc(size_buf * sizeof(char));
    if (!r->response.headers_buf.data) {
        say(S_ERROR ,"in %s:%d: Internal error. Can't allocate memory for header buffer\n", __FILE__, __LINE__);
        return false;
    }
    r->response.headers_buf.allocated = size_buf;
    
    r->response.body_buf.data = (char*) malloc(size_buf * sizeof(char));
    if (!r->response.body_buf.data) {
        say(S_ERROR ,"in %s:%d: Internal error. Can't allocate memory for body buffer\n", __FILE__, __LINE__);
        return false;
    }
    r->response.body_buf.allocated = size_buf;

    r->cond = (struct ipc_cond*) malloc(sizeof(struct ipc_cond));
    if (!r->cond) {
        say(S_ERROR ,"in %s:%d: Internal error. Can't allocate memory for cond variable\n", __FILE__, __LINE__);
        return false;
    }
    ipc_cond_create(r->cond);
    return true;
}



static inline
void
reset_request(request_t *r)
{
    assert(r);

    r->pool.busy = false;

    if (r->headers) {
        curl_slist_free_all(r->headers);
        r->headers = NULL;
    }

    r->response.headers_buf.written = 0;
    if (r->response.headers_buf.data)
        r->response.headers_buf.data[0] = 0;

    r->response.body_buf.written = 0;
    if (r->response.body_buf.data)
        r->response.body_buf.data[0] = 0;

    if (r->body) {
        free(r->body);
        r->body = NULL;
    }
    r->read = 0;
    r->sent = 0;

    if (r->easy) {
        curl_easy_cleanup(r->easy);
        r->easy = NULL;
    }

}


bool
request_pool_new(request_pool_t *p, struct curl_ctx_s *c, size_t s, size_t size_buffer)
{
    assert(p);
    assert(c);

    memset(p, 0, sizeof(request_pool_t));

    p->size = s;

    p->mem = (request_t *) malloc(p->size * sizeof(request_t));
    if (p->mem == NULL)
        goto error_exit;
    memset(p->mem, 0, p->size * sizeof(request_t));

    for (size_t i = 0; i < p->size; ++i) {
        if (!create_request(c, i, size_buffer, &p->mem[i]))
            goto error_exit;
    }

    return true;
error_exit:
    request_pool_free(p);
    return false;
}


void
request_pool_free(request_pool_t *p)
{
    assert(p);

    if (p->mem) {
        for (size_t i = 0; i < p->size; ++i) {
            request_t *r = &p->mem[i];
            reset_request(r);
            if (r->response.headers_buf.data) {
                free(r->response.headers_buf.data);
                r->response.headers_buf.data = NULL;
            }
            if (r->response.body_buf.data) {
                free(r->response.body_buf.data);
                r->response.body_buf.data = NULL;
            }
            
            if (r->cond){
                ipc_cond_destroy(r->cond);
                free(r->cond);
                r->cond = NULL;
            }
        }
        free(p->mem);
        p->mem = NULL;
    }
}


request_t*
request_pool_get_request(request_pool_t *p)
{
    assert(p);

    if (p->mem == NULL)
        return NULL;

    for (size_t i = 0; i < p->size; ++i) {

        if (!p->mem[i].pool.busy) {

            request_t *r = &p->mem[i];

            r->easy = curl_easy_init();
            if (r->easy == NULL)
                return NULL;

            ++r->curl_ctx->stat.active_requests;
            r->pool.busy = true;

            return r;
        }
    }

    return NULL;
}


void
request_pool_free_request(request_pool_t *p, request_t *r)
{
    if (r == NULL || p->mem == NULL)
        return;

    if (r->pool.busy) {
        --r->curl_ctx->stat.active_requests;
        curl_multi_remove_handle(r->curl_ctx->multi, r->easy);
    }

    reset_request(r);
}


size_t
request_pool_get_free_size(request_pool_t *p)
{
    size_t size = 0;

    if (p == NULL)
        return size;

    for (size_t i = 0; i < p->size; ++i) {
        if (!p->mem[i].pool.busy)
            ++size;
    }

    return size;
}
