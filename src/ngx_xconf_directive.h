/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef NGX_CONF_DIRECTIVE_H
#define NGX_CONF_DIRECTIVE_H

#include "ngx_xconf_common.h"

typedef struct {
    ngx_str_t        cachefile;
    ngx_flag_t       usecache;
    ngx_int_t        timeout; /* s */
    ngx_int_t        cachetime; /* s */
    ngx_str_t        uri;
    ngx_str_t        noscheme_uri;
    ngx_flag_t       evaluri;
    ngx_str_t        scheme;

    ngx_pool_t      *pool;

} ngx_xconf_ctx_t;

char * ngx_xconf_include_uri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char * ngx_xconf_include_uri_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);
char * ngx_xconf_include_uri_lua(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);
char * ngx_xconf_include_uri_luai(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);

#endif /* NGX_CONF_DIRECTIVE_H */

