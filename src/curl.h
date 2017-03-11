#ifndef DRIVER_H_INCLUDED
#define DRIVER_H_INCLUDED 1
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


#include <diag.h>
#include "curl/curl.h"
#include "small/ibuf.h"
#include "small/mempool.h"
#include <ipc.h>

/*
 * Structures {{{
*/



struct curl_ctx {

	struct ev_loop	*loop;
	struct ev_timer timer_event;

	CURLM	*multi;
	int 	still_running;

	struct mempool req_pool;
	struct mempool resp_pool;

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

	bool done;
};

/*
 * Structure header need to be filled before sending.
 * Two strings without delimeter.
 */
struct curl_header {
	const char *key;
	const char *value;
};

struct curl_request {

	/** Information associated with a specific easy handle */
	CURL *easy;

	/* Reference to curl context */
	struct curl_ctx *ctx;

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

	struct ipc_cond cond;

	const char *url;
	const char *method;
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
	const char *ca_path;

	/* File holding one or more certificates
	 * to verify the peer with*/
	const char *ca_file;

};


struct curl_response {
	int curl_code;
	int http_code;
	char *headers;
	char *body;
	const char *errmsg;
};

/*
 * }}} Structures
 */

/*
 * Curl context API
 * {{{
 */

/* max_conn - Maximum number of entries in the Connection cache */
/* pipeline - Set to true to enable pipelining for this multi handle */

struct curl_ctx*
curl_ctx_create(struct curl_ctx *,bool, long);

void
curl_ctx_destroy(struct curl_ctx *);

/* }}}
 */

/*
 * Request API
 * {{{
 */

/*
	 <curl_request_execute> This function does async HTTP request

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

struct curl_request*
curl_request_new(struct curl_ctx *ctx, size_t size_buf);

struct curl_response*
curl_request_execute(struct curl_request *);

void
curl_request_delete(struct curl_request *r);

static inline void
curl_response_destroy(struct curl_ctx *ctx ,struct curl_response *resp) {
	mempool_free(&ctx->resp_pool, resp);
}

static inline void
curl_set_method(struct curl_request *req, const char *method)
{
	req->method = method;
}

static inline void
curl_set_url(struct curl_request *req, const char *url)
{
	req->url = url;
}

int
curl_set_headers(struct curl_request *, struct curl_header *);

static inline void
curl_set_body(struct curl_request *req, const char *body)
{
	assert(body);
	req->body = body;
	req->read = strlen(body);
	req->sent = 0;
}

static inline void
curl_set_max_conns(struct curl_request *req, long max_conns)
{
	req->max_conns = max_conns;
}

static inline void
curl_set_keepalive_idle(struct curl_request *req, long keepalive_idle)
{
	req->keepalive_idle = keepalive_idle;
}

static inline void
curl_set_keepalive_interval(struct curl_request *req, long keepalive_interval)
{
	req->keepalive_interval = keepalive_interval;
}

static inline void
curl_set_low_speed_time(struct curl_request *req, long low_speed_time)
{
	req->low_speed_time = low_speed_time;
}

static inline void
curl_set_low_speed_limit(struct curl_request *req, long low_speed_limit)
{
	req->low_speed_limit = low_speed_limit;
}

static inline void
curl_set_read_timeout(struct curl_request *req, long read_timeout)
{
	req->read_timeout = read_timeout;
}

static inline void
curl_set_connect_timeout(struct curl_request *req, long connect_timeout)
{
	req->connect_timeout = connect_timeout;
}

static inline void
curl_set_dns_cache_timeout(struct curl_request *req, long dns_cache_timeout)
{
	req->dns_cache_timeout = dns_cache_timeout;
}

static inline void
curl_set_verbose(struct curl_request *req, bool curl_verbose)
{
	req->curl_verbose = curl_verbose;
}

static inline void
curl_set_ca_path(struct curl_request *req, const char *ca_path)
{
	req->ca_path = ca_path;
}

static inline void
curl_set_ca_file(struct curl_request *req, const char *ca_file)
{
	req->ca_file = ca_file;
}

static inline char*
curl_get_headers(struct curl_request *req)
{
	if (ibuf_used(&req->response.headers_buf) <= 0)
		return NULL;

	char *bufp = (char *) ibuf_alloc(&req->response.headers_buf, 1);
	if (!bufp) {
		diag_set(OutOfMemory, 1, "ibuf_alloc", "curl");
		return NULL;
	}
	*bufp = 0;
	return req->response.headers_buf.buf;
}

static inline
char*
curl_get_body(struct curl_request *req)
{
	if (ibuf_used(&req->response.body_buf) <= 0)
		return NULL;

	char *bufp = (char *) ibuf_alloc(&req->response.body_buf, 1);
	if (!bufp) {
		diag_set(OutOfMemory, 1, "ibuf_alloc", "curl");
		return NULL;
	}
	*bufp = 0;
	return req->response.body_buf.buf;
}

static inline
int
get_http_code(struct curl_request *r)
{
	return r->response.http_code;
}

static inline
const char*
get_errmsg(struct curl_request *r)
{
	return r->response.errmsg;
}

static inline
bool
curl_request_add_header(struct curl_request *c, const char *http_header)
{
	assert(c);
	assert(http_header);
	struct curl_slist *l = curl_slist_append(c->headers, http_header);
	if (l == NULL)
		return false;
	c->headers = l;
	return true;
}


/*
 * }}}
 */

/* Some methods used for Lua API
 * {{{
 */

/*
 * }}}
 */
#endif /* DRIVER_H_INCLUDED */
