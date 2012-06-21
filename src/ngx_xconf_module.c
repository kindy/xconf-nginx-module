/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

/*
 * 感谢 chaoslawful (王晓哲) 给予大量精神指引和技术支持
 */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_xconf_directive.h"

#if !defined(nginx_version) || nginx_version < 8054
#error "at least nginx 0.8.54 is required"
#endif


static ngx_command_t ngx_xconf_commands[] = {

    /*
     * 实现多数据源的 include(配置生成)
     *
     * include_uri -O main -t 3s -c -T 3d http://config-server/web1.conf;
     */
    { ngx_string("include_uri"),
      NGX_ANY_CONF|NGX_CONF_1MORE,
      ngx_xconf_include_uri,
      0,
      0,
      NULL },

    ngx_null_command
};


ngx_module_t  ngx_xconf_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_xconf_commands,                   /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

