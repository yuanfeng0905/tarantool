/*
 * Copyright (C) 2016-2017 Tarantool AUTHORS: please see AUTHORS file.
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

#include <assert.h>
#include "fiber.h"
#include "curl.h"
#include "lua/utils.h"
#include "small/mempool.h"
#include "box/errcode.h"

/** request and request pool API (Internal)
 * {{{
 */
static inline
int
curl_request_add_header(struct curl_request *c, const char *http_header)
{
	assert(c);
	assert(http_header);
	struct curl_slist *l = curl_slist_append(c->headers, http_header);
	if (l == NULL)
		return -1;
	c->headers = l;
	return 0;
}

static inline
int
curl_request_add_header_keepalive(struct curl_request *req)
{
	static char buf[30];

	assert(req);

	snprintf(buf, sizeof(buf) - 1, "Keep-Alive: timeout=%d",
			 (int) req->keepalive_idle);

	return curl_request_add_header(req, buf);
}

static inline
int
curl_request_add_header_content_length(struct curl_request *req)
{
	assert(req);
	char buf[38];
	snprintf(buf, sizeof(buf) - 1, "%s: %zu", "Content-Length", req->read);

	return curl_request_add_header(req, buf);
}

static inline
int
curl_request_add_header_accept(struct curl_request *req)
{
	assert(req);
	assert(req->easy);
	return curl_request_add_header(req, "Accept: */*");
}

static
CURLMcode
curl_request_start(struct curl_request *req);

/* }}}
 */

/* Internal structures
 * {{{
 */

struct curl_sock {
	/* Curl easy handler */
	CURL *easy;
	/* Reference to contextt*/
	struct curl_ctx *curl_ctx;
	/* libev watcher */
	struct ev_io ev;

	/* Descriptor to curl_socket */
	curl_socket_t sockfd;

	/* Action came from socket*/
	int action;
	/* Timeout for watcher */
	long timeout;
	/* Flag on socket callbacks */
	int evset;
};


/* }}}
 */


static void curl_timer_cb(EV_P_ struct ev_timer *w, int revents);

static inline
int
is_mcode_good(CURLMcode code)
{
	if (code == CURLM_OK)
		return 0;

	const char *s;

	switch(code) {
	case CURLM_BAD_HANDLE:
		s = "CURLM_BAD_HANDLE";
		break;
	case CURLM_BAD_EASY_HANDLE:
		s = "CURLM_BAD_EASY_HANDLE";
		break;
	case CURLM_OUT_OF_MEMORY:
		s = "CURLM_OUT_OF_MEMORY";
		break;
	case CURLM_INTERNAL_ERROR:
		s = "CURLM_INTERNAL_ERROR";
		break;
	case CURLM_UNKNOWN_OPTION:
		s = "CURLM_UNKNOWN_OPTION";
		break;
	case CURLM_LAST:
		s = "CURLM_LAST";
		break;
	default:
		s = "CURLM_unknown";
		break;
	case CURLM_BAD_SOCKET:
		s = "CURLM_BAD_SOCKET";
		/* ignore this error */
		return 0;
	}

	say_debug("ERROR: returns = %s", s);

	return -1;
}


/** Update the event timer after curl_multi library calls
 */
static int
curl_multi_timer_cb(CURLM *multi __attribute__((unused)),
				long timeout_ms,
				void *ctx)
{
	struct curl_ctx *l = (struct curl_ctx *) ctx;

	ev_timer_stop(l->loop, &l->timer_event);
	if (timeout_ms > 0) {
		ev_timer_init(&l->timer_event,
				curl_timer_cb,
				(double) (timeout_ms / 1000), 0.);
		ev_timer_start(l->loop, &l->timer_event);
	}
	else
		curl_timer_cb(l->loop, &l->timer_event, 0);
	return 0;
}


/** Check for completed transfers, and remove their easy handles
 */
static void
curl_check_multi_info(struct curl_ctx *l)
{
	char *eff_url;
	CURLMsg	 *msg;
	int msgs_left;
	struct curl_request *req;
	long http_code;

	while ((msg = curl_multi_info_read(l->multi, &msgs_left))) {

		if (msg->msg != CURLMSG_DONE)
			continue;

		CURL *easy = msg->easy_handle;
		CURLcode curl_code = msg->data.result;

		curl_easy_getinfo(easy, CURLINFO_PRIVATE, (void *) &req);
		curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
		curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

		say_debug("DONE: url = %s, curl_code = %d, http_code = %d",
				eff_url, curl_code, (int) http_code);

		if (curl_code != CURLE_OK)
			++l->stat.failed_requests;

		if (http_code == 200)
			++l->stat.http_200_responses;
		else
			++l->stat.http_other_responses;

		req->curl_code = (int) curl_code;
		req->http_code = (int) http_code;
		req->errmsg = curl_easy_strerror(curl_code);
		ipc_cond_signal(&req->cond);
	} /* while */
}


/** Called by libevent when we get action on a multi socket
 */
static void
curl_event_cb(EV_P_ struct ev_io *w, int revents)
{
	(void) loop;

	struct curl_ctx *l = (struct curl_ctx *) w->data;

	const int action = ( (revents & EV_READ ? CURL_POLL_IN : 0) |
				(revents & EV_WRITE ? CURL_POLL_OUT : 0) );
	CURLMcode rc = curl_multi_socket_action(l->multi, w->fd,
						action, &l->still_running);

	if (is_mcode_good(rc) < 0)
		++l->stat.failed_requests;

	curl_check_multi_info(l);

	if (l->still_running <= 0) {
		say_debug("last transfer done, kill timeout");
		ev_timer_stop(l->loop, &l->timer_event);
	}
}

/** Called by libevent when our timeout expires
 */
static void
curl_timer_cb(EV_P_ struct ev_timer *w, int revents __attribute__((unused)))
{
	(void) loop;

	say_debug("timer_cb: w = %p, revents = %i", (void *) w, revents);

	struct curl_ctx *l = (struct curl_ctx *) w->data;
	CURLMcode rc = curl_multi_socket_action(l->multi,
			CURL_SOCKET_TIMEOUT, 0, &l->still_running);
	if (is_mcode_good(rc) < 0)
		++l->stat.failed_requests;

	curl_check_multi_info(l);
}

/** Clean up the curl_sock structure
 */
static inline void
curl_remove_sock(struct curl_sock *f, struct curl_ctx *l)
{
	say_debug("removing socket");

	if (f == NULL)
		return;

	if (f->evset)
		ev_io_stop(l->loop, &f->ev);

	++l->stat.sockets_deleted;

	free(f);
}


/** Assign information to a curl_sock structure
 */
static inline void
curl_set_sock(struct curl_sock *f,
		curl_socket_t s,
		CURL *e,
		int act,
		struct curl_ctx *l)
{
	say_debug("set new socket");

	const int kind = ( (act & CURL_POLL_IN ? EV_READ : 0) |
					(act & CURL_POLL_OUT ? EV_WRITE : 0) );

	f->sockfd  = s;
	f->action  = act;
	f->easy	 = e;
	f->ev.data = l;
	f->evset = 1;

	if (f->evset)
		ev_io_stop(l->loop, &f->ev);

	ev_io_init(&f->ev, curl_event_cb, f->sockfd, kind);
	ev_io_start(l->loop, &f->ev);
}


/** Initialize a new curl_sock structure
 */
static int
curl_add_sock(curl_socket_t s, CURL *easy, int action, struct curl_ctx *l)
{
	struct curl_sock *fdp = (struct curl_sock *)
		malloc(sizeof(struct curl_sock));
	if (fdp == NULL)
		return -1;

	memset(fdp, 0, sizeof(struct curl_sock));

	fdp->curl_ctx = l;

	curl_set_sock(fdp, s, easy, action, l);

	curl_multi_assign(l->multi, s, fdp);

	++fdp->curl_ctx->stat.sockets_added;

	return 0;
}

/* Called on socket action */
static int
curl_sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
	struct curl_ctx *l = (struct curl_ctx *) cbp;
	struct curl_sock *fdp = (struct curl_sock *) sockp;

	static const char *whatstr[] = {
		"none", "IN", "OUT", "INOUT", "REMOVE" };

	say_debug("e = %p, s = %i, what = %s, cbp = %p, sockp = %p",
			e, s, whatstr[what], cbp, sockp);

	if (what == CURL_POLL_REMOVE)
		curl_remove_sock(fdp, l);
	else {
		if (fdp == NULL) {
			if (curl_add_sock(s, e, what, l) < 0)
				return 1;
			}
		else {
			say_debug("Changing action from = %s, to = %s",
					whatstr[fdp->action], whatstr[what]);
			curl_set_sock(fdp, s, e, what, l);
		}
	}

	return 0;
}

/* Called on read action. Sents body to server */
static size_t
curl_read_cb(void *ptr, size_t size, size_t nmemb, void *ctx)
{
	say_debug("size = %zu, nmemb = %zu", size, nmemb);

	struct curl_request *req = (struct curl_request *) ctx;
	const size_t total_size = size * nmemb;
	if (!req->body) {
		return total_size;
	}
	size_t to_send = total_size;
	if (req->sent + total_size > req->read)
		to_send = req->read - req->sent;
	memcpy(ptr, req->body + req->sent, to_send);
	req->sent += to_send;

	return to_send;
}

/* Internal function. Push piece of data to ibuf.
 * Used in write_cb and headers_cb */
static size_t
curl_push_buffer(struct ibuf *bufp, char *data, size_t size)
{
	assert(data);
	assert(bufp);

	char *p = (char *) ibuf_alloc(bufp, size);
	if (!p) {
		diag_set(OutOfMemory, size, "ibuf_alloc", "curl");
		return size;
	}
	memcpy(p, data, size);
	return size;
}

/* Called on write action.
 * Recieves data from server and writes it to buffer */
static size_t
curl_write_cb(char *ptr, size_t size, size_t nmemb, void *ctx)
{
	say_debug("size = %zu, nmemb = %zu", size, nmemb);

	struct curl_request *req = (struct curl_request *) ctx;
	const size_t bytes = size * nmemb;

	return curl_push_buffer(&req->body_buf, ptr, bytes);
}

/* Called on recieving headers action.
 * Recieves parsed headers from server and writes them to buffer */
static size_t
curl_header_cb(char *buffer, size_t size, size_t nitems, void *ctx)
{
	say_debug("size = %zu, mitems = %zu", size, nitems);
	struct curl_request *req = (struct curl_request *) ctx;
	const size_t bytes = size * nitems;
	return curl_push_buffer(&req->headers_buf, buffer, bytes);
}

/* Initiates the request */
static CURLMcode
curl_request_start(struct curl_request *req)
{
	assert(req);
	assert(req->easy);
	assert(req->ctx);

	if (req->max_conns > 0)
		curl_easy_setopt(req->easy, CURLOPT_MAXCONNECTS, req->max_conns);

#if (LIBCURL_VERSION_MAJOR >= 7 && \
	 LIBCURL_VERSION_MINOR >= 25 && \
	 LIBCURL_VERSION_PATCH >= 0 )

	if (req->keepalive_idle > 0 && req->keepalive_interval > 0) {

		curl_easy_setopt(req->easy, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(req->easy, CURLOPT_TCP_KEEPIDLE,
				req->keepalive_idle);
		curl_easy_setopt(req->easy,
				CURLOPT_TCP_KEEPINTVL, req->keepalive_interval);
		if (!curl_request_add_header(req, "Connection: Keep-Alive") &&
			curl_request_add_header_keepalive(req) < 0)
		{
			++req->ctx->stat.failed_requests;
			return CURLM_OUT_OF_MEMORY;
		}
	} else {
		if (curl_request_add_header(req, "Connection: close") < 0) {
			++req->ctx->stat.failed_requests;
			return CURLM_OUT_OF_MEMORY;
		}
	}

#else /* > 7.25.0 */

	if (req->keepalive_idle > 0 && req->keepalive_interval > 0) { }

#endif

	if (req->read_timeout > 0)
		curl_easy_setopt(req->easy, CURLOPT_TIMEOUT, req->read_timeout);

	if (req->connect_timeout > 0)
		curl_easy_setopt(req->easy,
				CURLOPT_CONNECTTIMEOUT, req->connect_timeout);

	if (req->dns_cache_timeout > 0)
		curl_easy_setopt(req->easy, CURLOPT_DNS_CACHE_TIMEOUT,
						 req->dns_cache_timeout);

	if (req->curl_verbose)
		curl_easy_setopt(req->easy, CURLOPT_VERBOSE, 1L);

	curl_easy_setopt(req->easy, CURLOPT_PRIVATE, (void *) req);

	curl_easy_setopt(req->easy, CURLOPT_READFUNCTION, curl_read_cb);
	curl_easy_setopt(req->easy, CURLOPT_READDATA, (void *) req);

	curl_easy_setopt(req->easy, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(req->easy, CURLOPT_WRITEDATA, (void *) req);

	curl_easy_setopt(req->easy, CURLOPT_HEADERFUNCTION, curl_header_cb);
	curl_easy_setopt(req->easy, CURLOPT_HEADERDATA, (void *) req);

	curl_easy_setopt(req->easy, CURLOPT_NOPROGRESS, 1L);

	curl_easy_setopt(req->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

	if (req->low_speed_time > 0)
		curl_easy_setopt(req->easy,
				CURLOPT_LOW_SPEED_TIME, req->low_speed_time);

	if (req->low_speed_limit > 0)
		curl_easy_setopt(req->easy,
				CURLOPT_LOW_SPEED_LIMIT, req->low_speed_limit);

	/* Headers have to be set right before add_handle() */
	if (req->headers != NULL)
		curl_easy_setopt(req->easy, CURLOPT_HTTPHEADER, req->headers);

	++req->ctx->stat.total_requests;

	CURLMcode rc = curl_multi_add_handle(req->ctx->multi, req->easy);
	if (is_mcode_good(rc) < 0) {
		++req->ctx->stat.failed_requests;
		return rc;
	}
	return rc;
}

/* Takes response from request object */
static struct curl_response*
curl_get_response(struct curl_request *req)
{
	assert(req);

	struct curl_response *resp = mempool_alloc(&req->ctx->resp_pool);
	if (!resp) {
		diag_set(OutOfMemory, sizeof(struct curl_response),
				"mempool_alloc", "curl");
		return NULL;
	}

	resp->curl_code = req->curl_code;
	resp->http_code = req->http_code;
	resp->errmsg = req->errmsg;
	resp->body = NULL;
	resp->headers = NULL;
	char *bufp;

	if (ibuf_used(&req->headers_buf) > 0) {

		bufp = (char *) ibuf_alloc(&req->headers_buf, 1);
		if (!bufp) {
			diag_set(OutOfMemory, 1, "ibuf_alloc", "curl");
			return NULL;
		}
		*bufp = 0;
		resp->headers = req->headers_buf.buf;
	}

	if (ibuf_used(&req->body_buf) > 0) {

		bufp = (char *) ibuf_alloc(&req->body_buf, 1);
		if (!bufp) {
			diag_set(OutOfMemory, 1, "ibuf_alloc", "curl");
			return NULL;
		}
		*bufp = 0;
		resp->body = req->body_buf.buf;
	}
	return resp;
}

/** lib C API {{{
 */

struct curl_ctx*
curl_ctx_create(struct curl_ctx *ctx, bool pipeline, long max_conns)
{

	assert(ctx);

	ctx->done = false;

	memset(ctx, 0, sizeof(struct curl_ctx));


	mempool_create(&ctx->req_pool, &cord()->slabc,
			sizeof(struct curl_request));
	mempool_create(&ctx->resp_pool, &cord()->slabc,
			sizeof(struct curl_response));
	ctx->loop = loop();
	if (ctx->loop == NULL)
		goto error_exit;

	ctx->multi = curl_multi_init();
	if (ctx->multi == NULL) {
		diag_set(SystemError, "failed to init multi handler");
		goto error_exit;
	}
	ev_timer_init(&ctx->timer_event, curl_timer_cb, 0., 0.);
	ctx->timer_event.data = (void *) ctx;

	curl_multi_setopt(ctx->multi, CURLMOPT_SOCKETFUNCTION, curl_sock_cb);
	curl_multi_setopt(ctx->multi, CURLMOPT_SOCKETDATA, (void *) ctx);

	curl_multi_setopt(ctx->multi, CURLMOPT_TIMERFUNCTION,
			curl_multi_timer_cb);
	curl_multi_setopt(ctx->multi, CURLMOPT_TIMERDATA, (void *) ctx);

	if (pipeline)
		curl_multi_setopt(ctx->multi, CURLMOPT_PIPELINING, 1L);

	if (max_conns > 0)
		curl_multi_setopt(ctx->multi, CURLMOPT_MAXCONNECTS, max_conns);

	return ctx;

error_exit:
	curl_ctx_destroy(ctx);
	return NULL;
}

void
curl_ctx_destroy(struct curl_ctx *ctx)
{
	if (ctx == NULL)
		return;
	if (ctx->multi != NULL)
		curl_multi_cleanup(ctx->multi);

	mempool_destroy(&ctx->req_pool);
	mempool_destroy(&ctx->resp_pool);
	ctx->done = true;
}

struct curl_response*
curl_request_execute(struct curl_request *req)
{
	const char *reason = "unknown error";
	if (!req->method) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
				"method must be not NULL string");
		return NULL;
	}

	if (!req->url) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
				"must be not NULL string");
		return NULL;
	}

	req->ctx->done = false;

	if (curl_request_add_header_content_length(req) < 0) {
		reason = "can't allocate memory (curl_request_add_header)";
		goto error_exit;
	}


	/* SSL/TLS cert  {{{ */
	if (req->ca_path)
		curl_easy_setopt(req->easy, CURLOPT_CAPATH, req->ca_path);

	if (req->ca_file)
		curl_easy_setopt(req->easy, CURLOPT_CAINFO, req->ca_file);
	/* }}} */

	curl_easy_setopt(req->easy, CURLOPT_PRIVATE, (void *) req);

	curl_easy_setopt(req->easy, CURLOPT_URL, req->url);
	curl_easy_setopt(req->easy, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(req->easy, CURLOPT_SSL_VERIFYPEER, 1);


	if (strncmp(req->method, "GET", sizeof("GET") - 1) == 0) {
		curl_easy_setopt(req->easy, CURLOPT_HTTPGET, 1L);
	}
	else if (strncmp(req->method, "HEAD", sizeof("HEAD") - 1) == 0) {
		curl_easy_setopt(req->easy, CURLOPT_NOBODY, 1L);
	}
	else if (strncmp(req->method, "POST", sizeof("POST") - 1) == 0) {
		if (curl_request_add_header_accept(req) < 0) {
			reason = "can't allocate memory (request_set_post)";
			goto error_exit;
		}
		curl_easy_setopt(req->easy, CURLOPT_POST, 1L);
	}
	else if (strncmp(req->method, "PUT", sizeof("PUT") - 1) == 0) {
		if (curl_request_add_header_accept(req) < 0) {
			reason = "can't allocate memory (request_set_put)";
			goto error_exit;
		}
		curl_easy_setopt(req->easy, CURLOPT_UPLOAD, 1L);
	}
	else if (
		strncmp(req->method, "OPTIONS", sizeof("OPTIONS") - 1) == 0 ||
		strncmp(req->method, "DELETE", sizeof("DELETE") - 1) == 0 ||
		strncmp(req->method, "TRACE", sizeof("TRACE") - 1) == 0 ||
		strncmp(req->method, "CONNECT", sizeof("CONNECT") - 1) == 0
		 ) {
		 curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, req->method);
	}
	else {
		reason = "method does not supported";
		goto error_exit;
	}

	CURLMcode mcode  = curl_request_start(req);
	if (mcode != CURLM_OK) {
		if (mcode != CURLM_LAST)
			reason = curl_multi_strerror(mcode);
		else
			reason = "Unknown error";
		goto error_exit;
	}
	ipc_cond_wait_timeout(&req->cond, TIMEOUT_INFINITY);

	return curl_get_response(req);
error_exit:
	say_error("%s", reason);
	curl_request_delete(req);
	return NULL;
}

int
curl_set_headers(struct curl_request *req, struct curl_header *hh)
{
	char tmp[4096];
	for (;hh->key; hh++) {
		snprintf(tmp, sizeof(tmp) - 1, "%s: %s",
				hh->key, hh->value);
		if (curl_request_add_header(req, tmp) < 0) {
			return -1;
		}
	}
	return 0;
}



struct curl_request*
curl_request_new(struct curl_ctx *ctx, size_t size_buf)
{
	assert(ctx);

	struct curl_request *req = (struct curl_request *)
				mempool_alloc(&ctx->req_pool);

	if (!req) {
		diag_set(OutOfMemory, sizeof(struct curl_request),
				"mempool_alloc", "curl");
		return NULL;
	}

	req->max_conns = -1;
	req->keepalive_idle = -1;
	req->keepalive_interval = -1;
	req->low_speed_time = -1;
	req->low_speed_limit = -1;
	req->read_timeout = -1;
	req->connect_timeout = -1;
	req->dns_cache_timeout = -1;
	req->curl_verbose = false;
	req->ca_path = NULL;
	req->ca_file = NULL;
	req->body = NULL;
	req->read = 0;
	req->sent = 0;
	req->headers = NULL;

	req->ctx = ctx;

	req->easy = curl_easy_init();

	if (req->easy == NULL)
	{
		diag_set(SystemError, "failed to init easy handler");
		mempool_free(&ctx->req_pool, req);
		return NULL;
	}

	ibuf_create(&req->headers_buf, &cord()->slabc, size_buf);

	ibuf_create(&req->body_buf, &cord()->slabc, size_buf);

	ipc_cond_create(&req->cond);

	++req->ctx->stat.active_requests;

	return req;
}

void
curl_request_delete(struct curl_request *req)
{
	if (req->headers) {
		curl_slist_free_all(req->headers);
		req->headers = NULL;
	}

	if (req->easy) {
		curl_easy_cleanup(req->easy);
		req->easy = NULL;
	}

	ibuf_destroy(&req->headers_buf);
	ibuf_destroy(&req->body_buf);

	ipc_cond_destroy(&req->cond);
	--req->ctx->stat.active_requests;
	mempool_free(&req->ctx->req_pool, req);
}
/* }}}//lib C API
 * */
