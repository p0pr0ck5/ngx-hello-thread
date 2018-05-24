/*
 * Copyright (C) Robert Paprocki
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_string.h>
#include <ngx_thread_pool.h>
#include <unistd.h>


typedef struct {
    int                 foo;
    ngx_http_request_t  *r;
} hello_thread_ctx_t;


typedef struct {
    ngx_flag_t         enabled;
    ngx_str_t          thread_pool_name;
    ngx_thread_pool_t  *thread_pool;
} ngx_http_hello_thread_loc_conf_t;


static ngx_int_t ngx_http_hello_thread_handler(ngx_http_request_t *r);
static void *ngx_http_hello_thread_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_hello_thread_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_hello_thread_pool_name(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_http_hello_thread_init(ngx_conf_t *cf);


static ngx_http_module_t ngx_http_hello_thread_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_http_hello_thread_init,             /* postconfiguration */

    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    ngx_http_hello_thread_create_loc_conf,  /* create location configuration */
    ngx_http_hello_thread_merge_loc_conf    /* merge location configuration */
};


static ngx_command_t ngx_http_hello_thread_commands[] = {
    {
        ngx_string("hello_thread"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_hello_thread_loc_conf_t, enabled),
        NULL
    },
    {
        ngx_string("hello_thread_pool_name"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_hello_thread_pool_name,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_hello_thread_loc_conf_t, thread_pool_name),
        NULL
    },
    ngx_null_command
};


ngx_module_t ngx_http_hello_thread_module = {
    NGX_MODULE_V1,
    &ngx_http_hello_thread_module_ctx,   /* module context */
    ngx_http_hello_thread_commands,      /* commands */
    NGX_HTTP_MODULE,                     /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


static void
hello_thread_func(void *data, ngx_log_t *log)
{
    hello_thread_ctx_t *ctx = data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "pre sleep %d", ctx->foo);

    sleep(ctx->foo);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "post sleep");
}


static void
hello_thread_completion(ngx_event_t *ev)
{
    hello_thread_ctx_t *ctx = ev->data;

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, ctx->r->connection->log, 0,
                  "completion, finalize %d", ctx->foo);

    /* move past us */
    ctx->r->phase_handler++;
    ngx_http_core_run_phases(ctx->r);
}


static ngx_int_t
ngx_http_hello_thread_handler(ngx_http_request_t *r)
{
    ngx_http_hello_thread_loc_conf_t  *ulcf;
    hello_thread_ctx_t                *ctx;
    ngx_thread_task_t                 *task;

    ulcf = ngx_http_get_module_loc_conf(r, ngx_http_hello_thread_module);

    if (!ulcf->enabled) {
        return NGX_DECLINED;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http thread access handler");

    task = ngx_thread_task_alloc(r->pool, sizeof(hello_thread_ctx_t));

    ctx = task->ctx;

    ctx->foo = 3;
    ctx->r = r;

    task->handler = hello_thread_func;
    task->event.handler = hello_thread_completion;
    task->event.data = ctx;

    if (ngx_thread_task_post(ulcf->thread_pool, task) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http thread access handler end");

    return NGX_DONE;
}


static void *
ngx_http_hello_thread_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_hello_thread_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_hello_thread_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_hello_thread_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_hello_thread_loc_conf_t  *prev = parent;
    ngx_http_hello_thread_loc_conf_t  *conf = child;

    ngx_conf_merge_off_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_str_value(conf->thread_pool_name, prev->thread_pool_name, "");

    return NGX_CONF_OK;
}


static char *
ngx_hello_thread_pool_name(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                         *value;
    ngx_thread_pool_t                 *tp;
    ngx_http_hello_thread_loc_conf_t  *llcf;

    llcf = (ngx_http_hello_thread_loc_conf_t *)conf;

    value = cf->args->elts;
    value++;

    tp = ngx_thread_pool_add(cf, value);
    llcf->thread_pool = tp;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_hello_thread_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_hello_thread_handler;

    return NGX_OK;
}
