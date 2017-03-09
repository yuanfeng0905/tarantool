/*
 * Copyright (C) 2016 - 2017 Tarantool AUTHORS: please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *	copyright notice, this list of conditions and the
 *	following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials
 *	provided with the distribution.
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

#include "say.h"
#include "curl/curl.h"
#include "fiber.h"
#include "small/ibuf.h"
/*
 * Structures {{{
*/

struct curl_args_t {
	/* Set to true to enable pipelining for this multi handle */
	bool pipeline;

	/* Maximum number of entries in the Connection cache */
	long max_conns;

	/* Size of pool of requests, number of request
	 * can be performed simultaneously */
	size_t pool_size;

	/* Size of buffers on reading response */
	size_t buffer_size;
};

/*
 * Structure header need to be filled before sending.
 * Two strings without delimeter.
 */
struct header {
	const char* key;
	const char* value;
};

struct curl_request_args_t {

	/* Max amount of cached alive Connections */
	long max_conns;

	/* Non-universal keepalive knobs (Linux, AIX, HP-UX, more) */
	long keepalive_idle;
	long keepalive_interval;

	/*Set the "low speed limit & time"
	 If the download receives less than "low speed limit" bytes/second
	 during "low speed time" seconds, the operations is aborted.
	 You could i.e if you have a pretty high speed Connection,
	 abort if it is less than 2000 bytes/sec during 20 seconds;
	 */

	long low_speed_time;
	long low_speed_limit;

	/* Time-out the read operation after this amount of seconds */
	long read_timeout;

	/* Time-out connects operations after this amount of seconds,
	 * if connects are OK within this time, then fine...
	 * This only aborts the Connect phase.
	 */
	long connect_timeout;

	/* DNS cache timeout */
	long dns_cache_timeout;

	/* Enable/Disable curl verbose mode */
	bool curl_verbose;

	/* Path to directory holding one or more certificates
	 * to verify the peer with*/
	const char* ca_path;

	/* File holding one or more certificates
	 * to verify the peer with*/
	const char* ca_file;

	/* Body of request*/
	const char* body;
	/* Headers or request*/
	struct header* headers;
};

struct curl_request_pool_t{
	struct	curl_request_t *mem;
	size_t	size;
};

struct internal_ctx_t {

	struct ev_loop	*loop;
	struct ev_timer timer_event;

	struct	curl_request_pool_t cpool;

	CURLM	*multi;
	int 	still_running;

	/* Various values of statistics, they are used only for all
	 * Connection in curl context */
	struct {
	uint64_t total_requests;
	uint64_t http_200_responses;
	uint64_t http_other_responses;
	size_t	 failed_requests;
	size_t	 active_requests;
	size_t	 sockets_added;
	size_t	 sockets_deleted;
	} stat;
};

struct curl_ctx_t{
	struct internal_ctx_t *internal_ctx;
	bool done;
};


struct curl_request_t {

	/* pool meta info */
	struct {
	size_t idx;
	bool busy;
	} pool;

	/** Information associated with a specific easy handle */
	CURL *easy;

	/* Reference to internal context */
	struct internal_ctx_t *internal_ctx;

	/* HTTP headers */
	struct curl_slist *headers;

	struct {
	/* Buffer for headers and response	*/
	struct ibuf headers_buf;
	struct ibuf body_buf;

	int curl_code;
	int http_code;
	const char *errmsg;
	} response;

	/* body to send to server and its length*/
	const char *body;
	size_t read;
	size_t sent;

	struct ipc_cond *cond;
};


/*
 * }}} Structures
 */

/*
 * Curl context API
 * {{{
 */
struct curl_ctx_t*
curl_new(bool pipeline, long max_conn, long pool_size, long buffer_size);

void
curl_delete(struct curl_ctx_t *ctx);

/* }}}
 */

/*
 * Request API
 * {{{
 */

static inline
void
curl_request_args_init(struct curl_request_args_t *a)
{
	assert(a);
	a->max_conns = -1;
	a->keepalive_idle = -1;
	a->keepalive_interval = -1;
	a->low_speed_time = -1;
	a->low_speed_limit = -1;
	a->read_timeout = -1;
	a->connect_timeout = -1;
	a->dns_cache_timeout = -1;
	a->curl_verbose = false;
	a->ca_path = NULL;
	a->ca_file = NULL;
	a->body = NULL;
	a->headers = NULL;
}

/*
	 <curl_send_request> This function does async HTTP request

	Parameters:

		method	- HTTP method, like GET, POST, PUT and so on
		url - HTTP url, like https://tarantool.org/doc
		req_args- this is a structure of options.

			ca_path - a path to ssl certificate dir;

			ca_file - a path to ssl certificate file;

			headers - a structure of HTTP headers;

			body - body of HTTP request;

			max_conns - max amount of cached alive connections;

			keepalive_idle & keepalive_interval - non-universal
				keepalive knobs (Linux, AIX, HP-UX, more);

			low_speed_time & low_speed_limit -
				If the download receives less than
				"low speed limit" bytes/second
				during "low speed time" seconds,
				the operations is aborted.
				You could i.e if you have
				a pretty high speed connection, abort if
				it is less than 2000 bytes/sec during 20 seconds

			read_timeout - Time-out the read operation
					after this amount of seconds;

			connect_timeout - Time-out connect operations after
					this amount of seconds, if connects are;
					OK within this time, then fine...
					This only aborts the connect phase;

			dns_cache_timeout - DNS cache timeout;

			curl_verbose - make libcurl verbose!;

		Returns:
			 pointer structure curl_request_t
		You can get needed data with according functions
*/

struct curl_request_t*
curl_send_request(struct curl_ctx_t *ctx, const char* method, const char* url,
		const struct curl_request_args_t* args);

static inline
char*
get_headers(struct curl_request_t* r)
{
	if (ibuf_used(&r->response.headers_buf) <= 0)
		return NULL;

	char* p = (char*) ibuf_alloc(&r->response.headers_buf, 1);
	if (!p) {
		say(S_ERROR, "in %s:%d \
				can't allocate memory for input buffer\n",
				__FILE__, __LINE__);
		return NULL;
	}
	*p = 0;
	return r->response.headers_buf.buf;
}

static inline
char*
get_body(struct curl_request_t* r)
{
	if (ibuf_used(&r->response.body_buf) <= 0)
		return NULL;

	char* p = (char*) ibuf_alloc(&r->response.body_buf, 1);
	if (!p) {
		say(S_ERROR, "in %s:%d \
				can't allocate memory for input buffer\n",
				__FILE__, __LINE__);
		return NULL;
	}
	*p = 0;
	return r->response.body_buf.buf;
}

static inline
int
get_http_code(struct curl_request_t* r)
{
	return r->response.http_code;
}

static inline
const char*
get_errmsg(struct curl_request_t* r)
{
	return r->response.errmsg;
}

static inline
bool
curl_request_add_header(struct curl_request_t *c, const char *http_header)
{
	assert(c);
	assert(http_header);
	struct curl_slist *l = curl_slist_append(c->headers, http_header);
	if (l == NULL)
		return false;
	c->headers = l;
	return true;
}

void
curl_request_delete(struct curl_ctx_t* ctx, struct curl_request_t *r);

/*
 * }}}
 */

/* Some methods used for Lua API
 * {{{
 */

struct internal_ctx_t*
curl_ctx_new(const struct curl_args_t *a);

void
curl_ctx_delete(struct internal_ctx_t *l);

/* request pool API
 * used for stat info
 */

size_t
curl_request_pool_get_free_size(struct curl_request_pool_t *p);

void
curl_request_pool_free_request(struct curl_request_pool_t *p,
		struct curl_request_t *c);

/*
 * }}}
 */
#endif /* DRIVER_H_INCLUDED */
