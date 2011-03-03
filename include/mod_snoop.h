/*
 * mod_snoop.h - A support header for mod_snoop
 * Copyright (C) 2011 Michael Spiegle (mike@nauticaltech.com)
 *
 */

#ifndef _MOD_SNOOP_H_
#define _MOD_SNOOP_H_

#include <apr_buckets.h>
#include <apr_pools.h>

typedef enum {
	SNOOP_REQUEST_START,
	SNOOP_WANT_KEY,
	SNOOP_WANT_VALUE,
	SNOOP_REQUEST_END
} snoop_state_t;

typedef struct {
	apr_pool_t* pool;
	apr_bucket_brigade* bb;
	snoop_state_t state;
} snoop_filter_ctx_t;

#endif
