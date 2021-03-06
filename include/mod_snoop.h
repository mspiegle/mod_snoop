/*
 * mod_snoop.h - A support header for mod_snoop
 * Copyright (C) 2011 Michael Spiegle (mike@nauticaltech.com)
 *
 */

#ifndef _MOD_SNOOP_H_
#define _MOD_SNOOP_H_

#include <apr_buckets.h>
#include <apr_pools.h>
#include <httpd.h>
#include <http_config.h>

//defaults
const apr_port_t snoop_default_port = 9876;
const char snoop_filter_name[] = "SNOOP";

module AP_MODULE_DECLARE_DATA snoop_module;

typedef struct {
	apr_pool_t* pool;
	apr_bucket_brigade* bb;
	apr_socket_t* socket;
} snoop_filter_ctx_t;

typedef struct {
	apr_sockaddr_t* target;
} snoop_server_config_t;

#endif
