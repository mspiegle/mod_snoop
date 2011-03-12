/*
 *
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
#include <apr_strings.h>

static apr_status_t
capture_filter(ap_filter_t* f, apr_bucket_brigade* bb, ap_input_mode_t mode,
               apr_read_type_e block, apr_off_t readbytes) {

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
	             "capture_filter(): MODE[%d] BLOCK[%d] BYTES[%lu]",
	             mode, block, readbytes);

	//get config
	snoop_server_config_t* conf;
	conf = ap_get_module_config(f->c->base_server->module_config, &snoop_module);

	//we're only concerned with AP_MODE_GETLINE
	if (mode != AP_MODE_GETLINE) {
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
		             "capture_filter(): (mode != AP_MODE_GETLINE), returning");
		return ap_get_brigade(f->next, bb, mode, block, readbytes);
	}

	//setup the filter ctx
	apr_status_t ret;
	snoop_filter_ctx_t* ctx = f->ctx;
	char message[128];
	if (ctx == NULL) {
		// allocate ctx
		f->ctx = ctx = apr_pcalloc(f->c->pool, sizeof(snoop_filter_ctx_t));

		//create a subpool for this filter
		if (APR_SUCCESS != (ret = apr_pool_create(&(ctx->pool), f->c->pool))) {
			ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
			             "capture_filter(): couldn't create pool");
			return ret;
		}

		//create a bucket brigade
		ctx->bb = apr_brigade_create(ctx->pool, f->c->bucket_alloc);

		//create socket for later use
		ret = apr_socket_create(&(ctx->socket), APR_INET, SOCK_DGRAM,
                            APR_PROTO_UDP, ctx->pool);
		if (ret != APR_SUCCESS) {	
			ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
			             "capture_filter(): couldn't allocate udp socket");
			return ret;
		}
	}

	//at this point, we have reason to believe that we should snoop this request
	if (APR_SUCCESS != (ret = ap_get_brigade(f->next, bb, mode,
	                                         block, readbytes))) {
		ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
		             "capture_filter(): ap_get_brigade(): %s",
		             apr_strerror(ret, message, sizeof(message)));
		return ret;
	}

	//if we got this far, then we have bb data and a valid ctx
	const char* bucket_data;
	apr_size_t br;
	apr_bucket* b;
	b = APR_BRIGADE_FIRST(bb);
	//iterate our incoming bucket brigade
	while (b != APR_BRIGADE_SENTINEL(bb)) {
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
		             "capture_filter(): processing a bucket");
		if (!APR_BUCKET_IS_METADATA(b)) {
			if (APR_SUCCESS == apr_bucket_read(b, &bucket_data, &br,
			                                   APR_BLOCK_READ)) {
				//check to see if this is the end of the request
				if (br == 2) {
					if (0 == strncmp(bucket_data, "\r\n", 2)) {
						//cleanup and ensure we don't run anymore
						ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
						             "capture_filter(): we found the end of the request");
						char* brigade_data;
						if (APR_SUCCESS != (ret = apr_brigade_pflatten(ctx->bb,
						                                               &brigade_data,
						                                               &br, ctx->pool))) {
							ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
							             "capture_filter(): couldn't pflatten brigade");
							return ret;
						}

						//send packet
						ret = apr_socket_sendto(ctx->socket, conf->target,
						                        0, brigade_data, &br);
						if (ret != APR_SUCCESS) {
							ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
							             "capture_filter(): couldn't send packet");
							return ret;
						}

						//we need to cleanup our ctx because it could get used for
						//a keepalive request
						ret = apr_brigade_cleanup(ctx->bb);
						if (ret != APR_SUCCESS) {
							ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
							             "capture_filter(): couldn't purge ctx->bb");
						}
						ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
						             "capture_filter(): finished snooping, returning");

						return APR_SUCCESS;
					}
				}

				//otherwise, keep appending input buckets to our temp bb
				apr_bucket* temp = NULL;
				if (APR_SUCCESS != (ret = apr_bucket_copy(b, &temp))) {
					ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
					             "capture_filter(): failed to copy bucket");
					return ret;
				}

				if (APR_SUCCESS != (ret = apr_bucket_setaside(temp, ctx->pool))) {
					ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
					             "capture_filter(): failed to setaside bucket");
					return ret;
				}

				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
				             "capture_filter(): adding bucket to temp brigade");
				APR_BRIGADE_INSERT_TAIL(ctx->bb, temp);
			}
		}

		//load next bucket
		b = APR_BUCKET_NEXT(b);
	}
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
	             "capture_filter(): finished a filter iteration, returning");

	return APR_SUCCESS;
}


static const char*
config_server(cmd_parms* cmd, void* p, const char* arg1, const char* arg2) {
	//get server config
	snoop_server_config_t* conf;
	conf = ap_get_module_config(cmd->server->module_config, &snoop_module);

	//convert port to number
	apr_port_t port = 0;
	port = apr_atoi64(arg2);

	//if port is invalid, use default
	if (port < 1 || port > 65535) {
		port = snoop_default_port;
	}

	//lets build an apr_sockaddr_t
	//apr_sockaddr_info_get() does a hostname lookup for us, so we'll use it
	//to validate that the hostname we got was valid
	char message[128];
	apr_status_t ret;
	ret = apr_sockaddr_info_get(&(conf->target), arg1, APR_UNSPEC,
	                            port, 0, cmd->pool);
	if (ret != APR_SUCCESS) {
		return apr_psprintf(cmd->pool, "config_server(): %s",
		                    apr_strerror(ret, message, sizeof(message)));
	}

	return NULL;
}

static void*
create_server_config(apr_pool_t* pool, server_rec* server) {
	snoop_server_config_t* conf;
	conf = apr_pcalloc(pool, sizeof(snoop_server_config_t));

	conf->target = NULL;

	return conf;
}

static const
command_rec commands[] = {
	AP_INIT_TAKE2("SnoopServer", config_server, NULL, RSRC_CONF,
	              "Usage: SnoopServer hostname port"),
	{ NULL }
};

static int
pre_connection(conn_rec* c, void* csd) {
	//this is the only way we can add our filter on a per-connection basis
	ap_add_input_filter("SNOOP", NULL, NULL, c);
	return OK;
}

static void
register_hooks(apr_pool_t* pool) {
	ap_register_input_filter("SNOOP", capture_filter, NULL,
	                         AP_FTYPE_CONNECTION);
	ap_hook_pre_connection(pre_connection, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA snoop_module = {
	STANDARD20_MODULE_STUFF,
	NULL,
	NULL,
	create_server_config,
	NULL,
	commands,
	register_hooks
};
