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

#include <ipc.h>
#include "curl.h"
#include "lua/utils.h"


/** request and request pool API (Internal)
 * {{{
 */

static
bool
curl_request_pool_new(struct curl_request_pool_t *p, struct internal_ctx_t *c,
		size_t s, size_t buffer_size);

static
void
curl_request_pool_delete(struct curl_request_pool_t *p);


static inline
struct curl_request_t*
curl_request_new(struct internal_ctx_t *ctx);

static inline
bool
curl_request_add_header_keepalive(struct curl_request_t *r,
		const struct curl_request_args_t *a)
{
	static char buf[255];

	assert(r);
	assert(a);

	snprintf(buf, sizeof(buf) - 1, "Keep-Alive: timeout=%d",
			 (int) a->keepalive_idle);

	if (!curl_request_add_header(r, buf))
		return false;
	return true;
}

static inline
bool
curl_request_add_header_CL(struct curl_request_t *r,
		const struct curl_request_args_t *a)
{
	assert(r);
	assert(a);
	char buf[20];
	if (!a->body) {
		snprintf(buf, sizeof(buf) - 1,
		"%s: %i", "Content-Length", 0);
	} else {
		snprintf(buf, sizeof(buf) - 1,
		"%s: %zu", "Content-Length", strlen(a->body));
	}

	if (!curl_request_add_header(r, buf))
		return false;
	return true;
}

static inline
bool
curl_request_add_header_accept(struct curl_request_t *r)
{
	assert(r);
	assert(r->easy);
	if (!curl_request_add_header(r, "Accept: */*"))
		return false;
	return true;
}

static
CURLMcode
curl_request_start(struct curl_request_t *r, 
		const struct curl_request_args_t *a);

/* }}}
 */

/* Internal structures
 * {{{
 */

struct curl_sock_t {
	CURL *easy;
	struct internal_ctx_t *internal_ctx;
	struct ev_io ev;

	curl_socket_t sockfd;

	int action;
	long timeout;
	int evset;
};

/* }}}
 */

/** lib C API {{{
 */

struct curl_ctx_t*
curl_new(bool pipeline, long max_conn, long pool_size, long buffer_size)
{
	struct curl_ctx_t *ctx =
		(struct curl_ctx_t *) malloc (sizeof(struct curl_ctx_t));

	if (ctx == NULL) {
		say_error("In:%s:%d: Can't allocate memory for curl context",
				__FILE__, __LINE__);
		return ctx;
	}

	ctx->internal_ctx = NULL;
	ctx->done	 = false;

	struct curl_args_t args = {
		.pipeline = pipeline,
		.max_conns = max_conn,
		.pool_size = pool_size,
		.buffer_size = buffer_size
	};

	ctx->internal_ctx = curl_ctx_new(&args);
	if (ctx->internal_ctx == NULL) {
		say_error("In %s:%d: curl_new failed", __FILE__, __LINE__);
		return NULL;
	}
	return ctx;
}

void
curl_delete(struct curl_ctx_t *ctx)
{
	if (ctx == NULL)
		return;

	ctx->done = true;

	curl_ctx_delete(ctx->internal_ctx);
}

struct curl_request_t*
curl_send_request(struct curl_ctx_t *ctx, const char* method, const char* url,
		const struct curl_request_args_t* req_args)
{
	const char *reason = "unknown error";
	assert(ctx);
	assert(method);
	assert(url);
	assert(req_args);

	ctx->done = false;
	if (ctx->done) {
		say_error("curl stopped");
		return NULL;
	}

	struct curl_request_t *r = curl_request_new(ctx->internal_ctx);
	if (r == NULL) {
		say_error("can't get request obj from pool");
		return NULL;
	}

	if (!curl_request_add_header_CL(r, req_args)) {
		reason = "can't allocate memory (curl_request_add_header)";
		goto error_exit;
	}

	if (req_args->body) {
		r->read = strlen(req_args->body);
		r->body = req_args->body;
	}

	if (req_args->headers) {
		struct header *h = req_args->headers;
		char tmp[4096];
		for (;h->key; h++) {
			snprintf(tmp, sizeof(tmp) - 1, "%s: %s",
					h->key, h->value);
			if (!curl_request_add_header(r, tmp)) {
				reason = "can't allocate memory\
					  (curl_request_add_header)";
				goto error_exit;
			}
		}
	}
	/* SSL/TLS cert  {{{ */
	if (req_args->ca_path)
		curl_easy_setopt(r->easy, CURLOPT_CAPATH, req_args->ca_path);

	if (req_args->ca_file)
		curl_easy_setopt(r->easy, CURLOPT_CAINFO, req_args->ca_file);
	/* }}} */

	curl_easy_setopt(r->easy, CURLOPT_PRIVATE, (void *) r);

	curl_easy_setopt(r->easy, CURLOPT_URL, url);
	curl_easy_setopt(r->easy, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(r->easy, CURLOPT_SSL_VERIFYPEER, 1);


	if (strncmp(method, "GET", sizeof("GET") - 1) == 0) {
		curl_easy_setopt(r->easy, CURLOPT_HTTPGET, 1L);
	}
	else if (strncmp(method, "HEAD", sizeof("HEAD") - 1) == 0) {
		curl_easy_setopt(r->easy, CURLOPT_NOBODY, 1L);
	}
	else if (strncmp(method, "POST", sizeof("POST") - 1) == 0) {
		if (!curl_request_add_header_accept(r)) {
			reason = "can't allocate memory (request_set_post)";
			goto error_exit;
		}
		curl_easy_setopt(r->easy, CURLOPT_POST, 1L);
	}
	else if (strncmp(method, "PUT", sizeof("PUT") - 1) == 0) {
		if (!curl_request_add_header_accept(r)) {
			reason = "can't allocate memory (request_set_put)";
			goto error_exit;
		}
		curl_easy_setopt(r->easy, CURLOPT_UPLOAD, 1L);
	}
	else if (strncmp(method, "OPTIONS", sizeof("OPTIONS") - 1) == 0) {
		 curl_easy_setopt(r->easy, CURLOPT_CUSTOMREQUEST, "OPTIONS");
	}
	else if (strncmp(method, "DELETE", sizeof("DELETE") - 1) == 0) {
		 curl_easy_setopt(r->easy, CURLOPT_CUSTOMREQUEST, "DELETE");
	}
	else if (strncmp(method, "TRACE", sizeof("TRACE") - 1) == 0) {
		 curl_easy_setopt(r->easy, CURLOPT_CUSTOMREQUEST, "TRACE");
	}
	else if (strncmp(method, "CONNECT", sizeof("CONNECT") - 1) == 0) {
		 curl_easy_setopt(r->easy, CURLOPT_CUSTOMREQUEST, "CONNECT");
	}
	else {
		reason = "method does not supported";
		goto error_exit;
	}

	CURLMcode mcode  = curl_request_start(r, req_args);
	if (mcode != CURLM_OK) {
		if (mcode != CURLM_LAST)
			reason = curl_multi_strerror(mcode);
		else
			reason = "Unknown error";
		goto error_exit;
	}
	ipc_cond_wait_timeout(r->cond, TIMEOUT_INFINITY);

	return r;
error_exit:
	say_error("%s", reason);
	curl_request_delete(ctx, r);
	return NULL;
}

void
curl_request_delete(struct curl_ctx_t* ctx, struct curl_request_t *r)
{
	curl_request_pool_free_request(&ctx->internal_ctx->cpool, r);
}

/* }}}
 * */


#define is_mcode_good(mcode) is_mcode_good_(__FUNCTION__, (mcode))
static void curl_timer_cb(EV_P_ struct ev_timer *w, int revents);

static inline
bool
is_mcode_good_(const char *where __attribute__((unused)),
				 CURLMcode code)
{
	if (code == CURLM_OK)
		return true;

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
		return true;
	}

	say_info("ERROR: %s returns = %s", where, s);
	if (code == CURLM_BAD_SOCKET)
		return true;

	return false;
}


/** Update the event timer after curl_multi library calls
 */
static
int
curl_multi_timer_cb(CURLM *multi __attribute__((unused)),
				long timeout_ms,
				void *ctx)
{
	struct internal_ctx_t *l = (struct internal_ctx_t *) ctx;

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
static
void
curl_check_multi_info(struct internal_ctx_t *l)
{
	char *eff_url;
	CURLMsg	 *msg;
	int msgs_left;
	struct curl_request_t *r;
	long http_code;

	while ((msg = curl_multi_info_read(l->multi, &msgs_left))) {

		if (msg->msg != CURLMSG_DONE)
			continue;

		CURL *easy = msg->easy_handle;
		CURLcode curl_code = msg->data.result;

		curl_easy_getinfo(easy, CURLINFO_PRIVATE, (void *) &r);
		curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
		curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

		say_info("DONE: url = %s, curl_code = %d, http_code = %d",
				eff_url, curl_code, (int) http_code);

		if (curl_code != CURLE_OK)
			++l->stat.failed_requests;

		if (http_code == 200)
			++l->stat.http_200_responses;
		else
			++l->stat.http_other_responses;

		r->response.curl_code = (int) curl_code;
		r->response.http_code = (int) http_code;
		r->response.errmsg = curl_easy_strerror(curl_code);
		ipc_cond_signal(r->cond);
	} /* while */
}


/** Called by libevent when we get action on a multi socket
 */
static
void
curl_event_cb(EV_P_ struct ev_io *w, int revents)
{
	(void) loop;

	struct internal_ctx_t *l = (struct internal_ctx_t*) w->data;

	const int action = ( (revents & EV_READ ? CURL_POLL_IN : 0) |
				(revents & EV_WRITE ? CURL_POLL_OUT : 0) );
	CURLMcode rc = curl_multi_socket_action(l->multi, w->fd,
						action, &l->still_running);

	if (!is_mcode_good(rc))
		++l->stat.failed_requests;

	curl_check_multi_info(l);

	if (l->still_running <= 0) {
		say_info("last transfer done, kill timeout");
		ev_timer_stop(l->loop, &l->timer_event);
	}
}


/** Called by libevent when our timeout expires
 */
static
void
curl_timer_cb(EV_P_ struct ev_timer *w, int revents __attribute__((unused)))
{
	(void) loop;

	say_info("timer_cb: w = %p, revents = %i", (void *) w, revents);

	struct internal_ctx_t *l = (struct internal_ctx_t *) w->data;
	CURLMcode rc = curl_multi_socket_action(l->multi,
			CURL_SOCKET_TIMEOUT, 0, &l->still_running);
	if (!is_mcode_good(rc))
		++l->stat.failed_requests;

	curl_check_multi_info(l);
}

/** Clean up the curl_sock_t structure
 */
static inline
void
curl_remove_sock(struct curl_sock_t *f, struct internal_ctx_t *l)
{
	say_info("removing socket");

	if (f == NULL)
		return;

	if (f->evset)
		ev_io_stop(l->loop, &f->ev);

	++l->stat.sockets_deleted;

	free(f);
}


/** Assign information to a curl_sock_t structure
 */
static inline
void
curl_set_sock(struct curl_sock_t *f,
		curl_socket_t s,
		CURL *e,
		int act,
		struct internal_ctx_t *l)
{
	say_info("set new socket");

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


/** Initialize a new curl_sock_t structure
 */
static
bool
curl_add_sock(curl_socket_t s, CURL *easy, int action, struct internal_ctx_t *l)
{
	struct curl_sock_t *fdp = (struct curl_sock_t *)
		malloc(sizeof(struct curl_sock_t));
	if (fdp == NULL)
		return false;

	memset(fdp, 0, sizeof(struct curl_sock_t));


	fdp->internal_ctx = l;

	curl_set_sock(fdp, s, easy, action, l);

	curl_multi_assign(l->multi, s, fdp);

	++fdp->internal_ctx->stat.sockets_added;

	return true;
}


/* CURLMOPT_SOCKETFUNCTION */
static
int
curl_sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
	struct internal_ctx_t *l = (struct internal_ctx_t*) cbp;
	struct curl_sock_t *fdp = (struct curl_sock_t*) sockp;

	static const char *whatstr[] = {
		"none", "IN", "OUT", "INOUT", "REMOVE" };

	say_info("e = %p, s = %i, what = %s, cbp = %p, sockp = %p",
			e, s, whatstr[what], cbp, sockp);

	if (what == CURL_POLL_REMOVE)
		curl_remove_sock(fdp, l);
	else {
		if (fdp == NULL) {
			if (!curl_add_sock(s, e, what, l))
				return 1;
			}
		else {
			say_info("Changing action from = %s, to = %s",
					whatstr[fdp->action], whatstr[what]);
			curl_set_sock(fdp, s, e, what, l);
		}
	}

	 return 0;
}


/** CURLOPT_WRITEFUNCTION / CURLOPT_READFUNCTION
 */
static
size_t
curl_read_cb(void *ptr, size_t size, size_t nmemb, void *ctx)
{
	say_info("size = %zu, nmemb = %zu", size, nmemb);

	struct curl_request_t *r = (struct curl_request_t *) ctx;
	const size_t total_size = size * nmemb;
	if (!r->body) {
		return total_size;
	}

	size_t to_send = total_size;
	if (r->sent + total_size > r->read)
		to_send = r->read - r->sent;
	memcpy(ptr, r->body + r->sent, to_send);
	r->sent += to_send;

	return to_send;
}

static
size_t
curl_push_buffer(struct ibuf* bufp, char *data, size_t size)
{
	assert(data);
	assert(bufp);

	char* p = (char*) ibuf_alloc(bufp, size);
	if (!p) {
		say(S_ERROR, "in %s:%d \
				can't allocate memory for input buffer\n",
				__FILE__, __LINE__);
		return size;
	}
	memcpy(p, data, size);
	return size;
}

static
size_t
curl_write_cb(char *ptr, size_t size, size_t nmemb, void *ctx)
{
	say_info("size = %zu, nmemb = %zu", size, nmemb);

	struct curl_request_t *r = (struct curl_request_t *) ctx;
	const size_t bytes = size * nmemb;

	return curl_push_buffer(&r->response.body_buf, ptr, bytes);
}


static
size_t
curl_header_cb(char *buffer, size_t size, size_t nitems, void *ctx)
{
	say_info("size = %zu, mitems = %zu", size, nitems);
	struct curl_request_t *r = (struct curl_request_t*) ctx;
	const size_t bytes = size * nitems;
	return curl_push_buffer(&r->response.headers_buf, buffer, bytes);
}

static
CURLMcode
curl_request_start(struct curl_request_t *r, const struct curl_request_args_t *a)
{
	assert(r);
	assert(a);
	assert(r->easy);
	assert(r->internal_ctx);

	if (a->max_conns > 0)
		curl_easy_setopt(r->easy, CURLOPT_MAXCONNECTS, a->max_conns);

#if (LIBCURL_VERSION_MAJOR >= 7 && \
	 LIBCURL_VERSION_MINOR >= 25 && \
	 LIBCURL_VERSION_PATCH >= 0 )

	if (a->keepalive_idle > 0 && a->keepalive_interval > 0) {

		curl_easy_setopt(r->easy, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(r->easy, CURLOPT_TCP_KEEPIDLE,
				a->keepalive_idle);
		curl_easy_setopt(r->easy,
				CURLOPT_TCP_KEEPINTVL, a->keepalive_interval);
		if (!curl_request_add_header(r, "Connection: Keep-Alive") &&
			!curl_request_add_header_keepalive(r, a))
		{
			++r->internal_ctx->stat.failed_requests;
			return CURLM_OUT_OF_MEMORY;
		}
	} else {
		if (!curl_request_add_header(r, "Connection: close")) {
			++r->internal_ctx->stat.failed_requests;
			return CURLM_OUT_OF_MEMORY;
		}
	}

#else /* > 7.25.0 */

	if (a->keepalive_idle > 0 && a->keepalive_interval > 0) { }

#endif

	if (a->read_timeout > 0)
		curl_easy_setopt(r->easy, CURLOPT_TIMEOUT, a->read_timeout);

	if (a->connect_timeout > 0)
		curl_easy_setopt(r->easy,
				CURLOPT_CONNECTTIMEOUT, a->connect_timeout);

	if (a->dns_cache_timeout > 0)
		curl_easy_setopt(r->easy, CURLOPT_DNS_CACHE_TIMEOUT,
						 a->dns_cache_timeout);

	if (a->curl_verbose)
		curl_easy_setopt(r->easy, CURLOPT_VERBOSE, 1L);

	curl_easy_setopt(r->easy, CURLOPT_PRIVATE, (void *) r);

	curl_easy_setopt(r->easy, CURLOPT_READFUNCTION, curl_read_cb);
	curl_easy_setopt(r->easy, CURLOPT_READDATA, (void *) r);

	curl_easy_setopt(r->easy, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(r->easy, CURLOPT_WRITEDATA, (void *) r);

	curl_easy_setopt(r->easy, CURLOPT_HEADERFUNCTION, curl_header_cb);
	curl_easy_setopt(r->easy, CURLOPT_HEADERDATA, (void*) r);

	curl_easy_setopt(r->easy, CURLOPT_NOPROGRESS, 1L);

	curl_easy_setopt(r->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

	if (a->low_speed_time > 0)
		curl_easy_setopt(r->easy,
				CURLOPT_LOW_SPEED_TIME, a->low_speed_time);

	if (a->low_speed_limit > 0)
		curl_easy_setopt(r->easy,
				CURLOPT_LOW_SPEED_LIMIT, a->low_speed_limit);

	/* Headers have to be set right before add_handle() */
	if (r->headers != NULL)
		curl_easy_setopt(r->easy, CURLOPT_HTTPHEADER, r->headers);

	++r->internal_ctx->stat.total_requests;

	CURLMcode rc = curl_multi_add_handle(r->internal_ctx->multi, r->easy);
	if (!is_mcode_good(rc)) {
		++r->internal_ctx->stat.failed_requests;
		return rc;
	}
	return rc;
}

struct internal_ctx_t*
curl_ctx_new(const struct curl_args_t *a)
{
	assert(a);

	struct internal_ctx_t *l =
		(struct internal_ctx_t *) malloc(sizeof(struct internal_ctx_t));

	if (l == NULL)
		return NULL;

	memset(l, 0, sizeof(struct internal_ctx_t));

	if (!curl_request_pool_new(&l->cpool, l, a->pool_size, a->buffer_size))
		goto error_exit;

	l->loop = loop();
	if (l->loop == NULL)
		goto error_exit;

	l->multi = curl_multi_init();
	if (l->multi == NULL)
		goto error_exit;

	ev_timer_init(&l->timer_event, curl_timer_cb, 0., 0.);
	l->timer_event.data = (void *) l;

	curl_multi_setopt(l->multi, CURLMOPT_SOCKETFUNCTION, curl_sock_cb);
	curl_multi_setopt(l->multi, CURLMOPT_SOCKETDATA, (void *) l);

	curl_multi_setopt(l->multi, CURLMOPT_TIMERFUNCTION, curl_multi_timer_cb);
	curl_multi_setopt(l->multi, CURLMOPT_TIMERDATA, (void *) l);

	if (a->pipeline)
		curl_multi_setopt(l->multi, CURLMOPT_PIPELINING, 1L);

	if (a->max_conns > 0)
		curl_multi_setopt(l->multi, CURLMOPT_MAXCONNECTS, a->max_conns);

	return l;

error_exit:
	curl_ctx_delete(l);
	return NULL;
}

void
curl_ctx_delete(struct internal_ctx_t *l)
{
	if (l == NULL)
		return;

	if (l->multi != NULL)
		curl_multi_cleanup(l->multi);

	curl_request_pool_delete(&l->cpool);

	free(l);
}

/* Implementation of Pool API
 */
static inline
void curl_request_reset(struct curl_request_t *r);

static inline
bool
curl_request_create(struct internal_ctx_t *ctx, size_t idx, size_t size_buf,
		struct curl_request_t *r)
{
	assert(ctx);
	assert(r);

	memset(r, 0, sizeof(struct curl_request_t));

	r->pool.idx = idx;
	r->internal_ctx = ctx;

	curl_request_reset(r);

	ibuf_create(&r->response.headers_buf, &cord()->slabc, size_buf);

	ibuf_create(&r->response.body_buf, &cord()->slabc, size_buf);

	r->cond = (struct ipc_cond*) malloc(sizeof(struct ipc_cond));
	if (!r->cond) {
		say(S_ERROR ,"in %s:%d: Internal error. \
				Can't allocate memory for cond variable\n",
				__FILE__, __LINE__);
		return false;
	}
	ipc_cond_create(r->cond);
	return true;
}

static inline
void
curl_request_reset(struct curl_request_t *r)
{
	assert(r);

	r->pool.busy = false;

	if (r->headers) {
		curl_slist_free_all(r->headers);
		r->headers = NULL;
	}


	ibuf_reset(&r->response.headers_buf);
	ibuf_reset(&r->response.body_buf);

	r->read = 0;
	r->sent = 0;

	if (r->easy) {
		curl_easy_cleanup(r->easy);
		r->easy = NULL;
	}

}

static
bool
curl_request_pool_new(struct curl_request_pool_t *p, struct internal_ctx_t *c,
		size_t s, size_t size_buffer)
{
	assert(p);
	assert(c);

	memset(p, 0, sizeof(struct curl_request_pool_t));

	p->size = s;

	p->mem =
		(struct curl_request_t *) malloc(p->size *
				sizeof(struct curl_request_t));
	if (p->mem == NULL)
		goto error_exit;
	memset(p->mem, 0, p->size * sizeof(struct curl_request_t));

	for (size_t i = 0; i < p->size; ++i) {
		if (!curl_request_create(c, i, size_buffer, &p->mem[i]))
			goto error_exit;
	}

	return true;
error_exit:
	curl_request_pool_delete(p);
	return false;
}

static inline
struct curl_request_t*
curl_request_new(struct internal_ctx_t *ctx)
{
	struct curl_request_pool_t p = ctx->cpool;

	if (p.mem == NULL)
		return NULL;

	for (size_t i = 0; i < p.size; ++i) {

		if (!p.mem[i].pool.busy) {

			struct curl_request_t *r = &p.mem[i];

			r->easy = curl_easy_init();
			if (r->easy == NULL)
				return NULL;

			++r->internal_ctx->stat.active_requests;
			r->pool.busy = true;

			return r;
		}
	}

	return NULL;
}

static
void
curl_request_pool_delete(struct curl_request_pool_t *p)
{
	assert(p);

	if (p->mem) {
		for (size_t i = 0; i < p->size; ++i) {
			struct curl_request_t *r = &p->mem[i];
			curl_request_reset(r);

			ibuf_destroy(&r->response.headers_buf);
			ibuf_destroy(&r->response.body_buf);

			if (r->cond) {
				ipc_cond_destroy(r->cond);
				free(r->cond);
				r->cond = NULL;
			}
		}
		free(p->mem);
		p->mem = NULL;
	}
}

void
curl_request_pool_free_request(struct curl_request_pool_t *p,
		struct curl_request_t *r)
{
	if (r == NULL || p->mem == NULL)
		return;

	if (r->pool.busy) {
		--r->internal_ctx->stat.active_requests;
		curl_multi_remove_handle(r->internal_ctx->multi, r->easy);
	}

	curl_request_reset(r);
}

size_t
curl_request_pool_get_free_size(struct curl_request_pool_t *p)
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
