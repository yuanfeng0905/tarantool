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


/* Context of curl */
struct curl_ctx {

	/* Libev loop and timer-watcher*/
	struct ev_loop	*loop;
	struct ev_timer timer_event;

	/* Curl multi handler*/
	CURLM	*multi;
	/* State of request; Internal use */
	int 	still_running;

	/* Memory pools for requests and responses */
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
	/* Name of header */
	const char *key;

	/* Value of header */
	const char *value;
};

struct curl_request {

	/* Information associated with a specific easy handle */
	CURL *easy;

	/* Reference to curl context */
	struct curl_ctx *ctx;

	/* HTTP headers */
	struct curl_slist *headers;

	/* Internal struct for storing response data */
	struct {
	/* Buffers for headers and body */
	struct ibuf headers_buf;
	struct ibuf body_buf;

	/* codes of response */
	int curl_code;
	int http_code;

	/* error message */
	const char *errmsg;
	} response;

	/* body to send to server, its length, number of bytes sent to server */
	const char *body;
	size_t read;
	size_t sent;

	/* condition varaible for internal conditional loop */
	struct ipc_cond cond;
	/*url to send request to */
	const char *url;

	/* HTTP method */
	const char *method;

	/* Max amount of cached alive Connections */
	long max_conns;

	/* Non-universal keepalive knobs (Linux, AIX, HP-UX, more) */
	long keepalive_idle;
	long keepalive_interval;

	/* Set the "low speed limit & time"
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
	 * to verify the peer with */
	const char *ca_path;

	/* File holding one or more certificates
	 * to verify the peer with */
	const char *ca_file;

};

/* Response structure. User gets it after executing request */
struct curl_response {
	/* Internal curl code */
	int curl_code;
	/* Http code */
	int http_code;
	/* Reference to string headers */
	char *headers;
	/* Reference to string body */
	char *body;
	/* Reference to error messsage */
	const char *errmsg;
};

/*
 * }}} Structures
 */

/*
 * Curl context API
 * {{{
 */

/**
 *\brief Creates new context.
 *\param ctx pointer to allocated memory of structure ctx
 *\param max_conn - Maximum number of entries in the Connection cache
 *\param pipeline - Set to true to enable pipelining for this multi handle
 *\return pointer to created context of NULL in case of error
 */
struct curl_ctx*
curl_ctx_create(struct curl_ctx *ctx, bool pipeline, long max_conns);

/**
 *\brief destroy context object.
 *\param ctx pointer to allocated memory of structure ctx
 *\details doesn't free allocated memory
 */
void
curl_ctx_destroy(struct curl_ctx *);

/* }}}
 */

/*
 * Request API
 * {{{
 */

/**
 * \brief Creates object of request
 * \param ctx - reference to context
 * \param size_buf - initial size of buffers on response
 * \return new request object
 */
struct curl_request*
curl_request_new(struct curl_ctx *ctx, size_t size_buf);

/**
 *\brief This function does async HTTP request
 *\param request - reference to request object with filled fields
 *\return pointer structure curl_response
 *\details User recieves the reference to object response,
 * which should be destroyed with curl_response_destroy() at the end
 * Don't delete the request object before handling response!
 * That will destroy some fields in response object.
 */
struct curl_response*
curl_request_execute(struct curl_request *);

/**
 * \brief Delete request object
 * \param request - reference to object
 */
void
curl_request_delete(struct curl_request *req);
/**
 * \brief Destroy the response object
 * \param ctx - reference to context
 * \param resp - reference to response object
 */
static inline void
curl_response_destroy(struct curl_ctx *ctx ,struct curl_response *resp) {
	mempool_free(&ctx->resp_pool, resp);
}

/**
 * Setter field method.
 * \param req - object request
 * \param method - HTTP method, like GET, POST, PUT and so on
 */
static inline void
curl_set_method(struct curl_request *req, const char *method)
{
	req->method = method;
}

/**
 * Setter field url.
 * \param req - object request
 * \param url - HTTP url, like https://tarantool.org/doc
 * */
static inline void
curl_set_url(struct curl_request *req, const char *url)
{
	req->url = url;
}

/**
 * Setter field headers.
 * \param req - object request
 * \param headers - pointer to array of struct header
 *  headers should be filled in advance
 */
int
curl_set_headers(struct curl_request *, struct curl_header *);

/* Other fields setters */

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


/*
 * }}} Request API
 */

#endif /* DRIVER_H_INCLUDED */
