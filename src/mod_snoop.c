/* 
 * mod_snoop.c - The main implementation of mod_snoop
 * Copyright (C) 2011 Michael Spiegle (mike@nauticaltech.com)
 *
 */

#include "mod_snoop.h"

#include <stdio.h>

#include <httpd.h>
#include <http_log.h>
#include <http_config.h>
#include <http_connection.h>
#include <apr_errno.h>
#include <apr_buckets.h>
#include <apr_pools.h>
#include <util_filter.h>

//static const char capture_filter_name[] = "snoop";

static apr_status_t
capture_filter(ap_filter_t* f, apr_bucket_brigade* bb, ap_input_mode_t mode,
               apr_read_type_e block, apr_off_t readbytes) {

	printf("Inside capture_filter(%d, %d, %d)\n", mode, block, readbytes);
	// we're only concerned with AP_MODE_GETLINE
	if (mode != AP_MODE_GETLINE) {
		return ap_get_brigade(f->next, bb, mode, block, readbytes);
	}

	// go ahead and fetch the brigade - deal with errors
	apr_status_t ret;
	if (APR_SUCCESS != (ret = ap_get_brigade(f->next, bb, mode,
	                                         block, readbytes))) {
		return ret;
	}

	// setup the filter ctx
	snoop_filter_ctx_t* ctx;
	if (NULL == f->ctx) {
		// allocate ctx
		f->ctx = ctx = apr_pcalloc(f->f->pool, sizeof(snoop_filter_ctx_t));

		// create a subpool for the bb
		apr_status_t ret;
		if (APR_SUCCESS != (ret = apr_pool_create(&(ctx->pool), f->r->pool))) {
			ap_log_error(APLOG_MARK, APLOG_WARNING, 0, f->r->server,
			             "capture_filter(): couldn't create pool");
		}

		// create a bucket brigade
		ctx->bb = apr_brigade_create(ctx->pool, f->c->bucket_alloc);

		// set start
		ctx->state = SNOOP_REQUEST_START;
	}

	// if we got this far, then we have bb data and a valid ctx
	const char* buf;
	apr_size_t br;
	apr_bucket* b;
	b = APR_BRIGADE_FIRST(bb);
	printf("Starting loop\n");
	while (b != APR_BRIGADE_SENTINEL(bb)) {
		printf("Doing another iteration\n");
		// show us the DATA
		if (!APR_BUCKET_IS_METADATA(b)) {
			if (APR_SUCCESS == apr_bucket_read(b, &buf, &br, APR_BLOCK_READ)) {
				//printf("%s\n", buf);
				// we should parse through the bucket until we find \r\n\r\n
				int x = 0;
				for (x = 0; x < br; x++) {
					
				}
			}
		}

		// load next bucket
		b = APR_BUCKET_NEXT(b);
	}

	return APR_SUCCESS;
}

static int
pre_connection(conn_rec* c, void* csd) {
	//ap_add_input_filter(capture_filter_name, NULL, NULL, c);
	ap_add_input_filter("SNOOP", NULL, NULL, c);
	printf("*** pre_connection ***\n");
	return OK;
}

static void*
create_server_config(apr_pool_t* pool, server_rec* server) {
	return NULL;
}

static void
register_hooks(apr_pool_t* pool) {
	ap_register_input_filter("SNOOP", capture_filter, NULL,
	                         AP_FTYPE_CONNECTION);
	ap_hook_pre_connection(pre_connection, NULL, NULL, APR_HOOK_MIDDLE);
	printf("*** register_hooks ***\n");
}

static const
command_rec commands[] = {
	{ NULL }
};

module AP_MODULE_DECLARE_DATA snoop_module = {
	STANDARD20_MODULE_STUFF,
	NULL,
	NULL,
	create_server_config,
	NULL,
	commands,
	register_hooks
};
