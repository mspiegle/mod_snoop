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

		//set start state
		ctx->state = SNOOP_REQUEST_START;
	}

	//if we already processed the request, then move on
	if (ctx->state == SNOOP_REQUEST_END) {
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server,
		             "capture_filter(): already processed, returning");
		return ap_get_brigade(f->next, bb, mode, block, readbytes);
	}

	//at this point, we have reason to believe that we should snoop this request
	if (APR_SUCCESS != (ret = ap_get_brigade(f->next, bb, mode,
	                                         block, readbytes))) {
		ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
		             "capture_filter(): couldn't get brigade");
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
						char* brigade_data;
						if (APR_SUCCESS != (ret = apr_brigade_pflatten(ctx->bb,
						                                               &brigade_data,
						                                               &br, ctx->pool))) {
							ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
							             "capture_filter(): couldn't pflatten brigade");
							return ret;
						}

						//get socket
						apr_socket_t* socket;
						if (APR_SUCCESS != (ret = apr_socket_create(&socket, APR_INET,
						                                            SOCK_DGRAM,
						                                            APR_PROTO_UDP,
						                                            ctx->pool))) {
							ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
							             "capture_filter(): couldn't allocate socket");
							return ret;
						}

						//send packet
						ret = apr_socket_sendto(socket, conf->target, 0, brigade_data, &br);
						if (ret != APR_SUCCESS) {
							ap_log_error(APLOG_MARK, APLOG_CRIT, 0, f->c->base_server,
							             "capture_filter(): couldn't send packet");
							return ret;
						}

						ctx->state = SNOOP_REQUEST_END;
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
config_server(cmd_parms* cmd, void* p, const char* arg) {
	//get server config
	snoop_server_config_t* conf;
	conf = ap_get_module_config(cmd->server->module_config, &snoop_module);

	//parse the argument which could be hostname:port, or just hostname
	//we'll make a copy first because we may modify this string
	char server[64];
	apr_cpystrn(server, arg, sizeof(server));

	//iterate through the string looking for the hostname/port
	char* iter = server;
	apr_port_t port = 0;
	while (*iter != '\0') {
		//if we find a :
		if (*iter == ':') {
			//break the string in 2
			*iter = '\0';
			//make sure the next character wouldn't be a null, the set the port
			//to be the next character
			if (*(iter + 1) != '\0') {
				port = apr_atoi64(iter + 1);
			}
			break;
		}
		++iter;
	}

	//if port is invalid, use default
	if (port < 1 || port > 65535) {
		port = snoop_default_port;
	}

	//assuming everything checks out, let's build an apr_sockaddr_t
	char message[128];
	apr_status_t ret;
	ret = apr_sockaddr_info_get(&(conf->target), server, APR_UNSPEC,
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
	AP_INIT_TAKE1("SnoopServer", config_server, NULL, RSRC_CONF,
	              "Sets a server to send requests to"),
	{ NULL }
};

static int
pre_connection(conn_rec* c, void* csd) {
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
