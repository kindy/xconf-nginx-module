/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef NGX_CONF_DIRECTIVE_H
#define NGX_CONF_DIRECTIVE_H

#include "ngx_xconf_common.h"
#include "ngx_xconf_util.lua.h"


typedef struct ngx_xconf_ctx_s ngx_xconf_ctx_t;

typedef struct {
    ngx_str_t        name;
    char            *(*handler)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);
    ngx_flag_t       enable;
    ngx_flag_t       usecache;
    ngx_flag_t       pre_usecache;
    ngx_flag_t       fail_usecache;
} ngx_xconf_scheme_t;


struct ngx_xconf_ctx_s {
    lua_State       *lua;
    ngx_file_t      *cachefile;
    ngx_int_t        timeout; /* s */
    ngx_int_t        pre_usecache; /* s */
    ngx_int_t        fail_usecache; /* s */
    ngx_str_t        uri;
    ngx_str_t        noscheme_uri;
    ngx_flag_t       evaluri;
    ngx_flag_t       keep_error_cachefile;
    ngx_xconf_scheme_t  *scheme;

    /* 以上由 scheme router 逻辑设置 */
    /* 以下由 scheme 处理函数设置，用于告知 scheme router 接下来做什么 */
    ngx_flag_t       fetch_fail;
    ngx_flag_t       do_cachefile;
};


char * ngx_xconf_include_uri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

/* NGX_CONF_OK, NGX_CONF_ERROR 都是 指针，所以可以 char * */
/* NGX_OK, NGX_ERROR 都是 int，所以函数定义是 int */
/* 视函数用途选择合适的返回值类型吧 */
int ngx_xconf_util_lua_pcall(ngx_conf_t *cf, lua_State *L, int nargs, int nresults, int errfunc, int keeperrmsg);

char * ngx_xconf_include_uri_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);

char * ngx_xconf_include_uri_http(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);
char * ngx_xconf_include_uri_lua(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);
char * ngx_xconf_include_uri_luai(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx);


#endif /* NGX_CONF_DIRECTIVE_H */

