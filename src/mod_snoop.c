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

static const char capture_filter_name[] = "snoop";

static apr_status_t
capture_filter(ap_filter_t* f, apr_bucket_brigade* bb, ap_input_mode_t mode,
               apr_read_type_e block, apr_off_t readbytes) {
	apr_bucket* b;
	apr_status_t ret;
	const char* buf;
	apr_size_t br;

	// we're only concerned with READBYTES
	if (mode != AP_MODE_READBYTES) {
		return ap_get_brigade(f->next, bb, mode, block, readbytes);
	}

	// go ahead and fetch the brigade - deal with errors
	if (APR_SUCCESS != (ret = ap_get_brigade(f->next, bb, mode,
	                                         block, readbytes))) {
		return ret;
	}

	// if we got this far, then bb is populated
	b = APR_BRIGADE_FIRST(bb);
	const char* buf;
	apr_size_t br;
	while (b != APR_BRIGADE_SENTINEL(bb)) {
		// show us the DATA
		if (!APR_BUCKET_IS_METADATA(b)) {
			if (APR_SUCCESS == apr_bucket_read(b, &buf, &br, APR_BLOCK_READ)) {
				printf("%s", buf);
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
	printf("*** Crazy ***");
	return OK;
}

static void*
create_server_config(apr_pool_t* pool, server_rec* server) {
	return NULL;
}

static void
register_hooks(apr_pool_t* pool) {
	ap_register_input_filter(capture_filter_name, capture_filter, NULL,
	                         AP_FTYPE_NETWORK);
	ap_hook_pre_connection(pre_connection, NULL, NULL, APR_HOOK_MIDDLE);
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
