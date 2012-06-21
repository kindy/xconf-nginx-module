/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef NGX_CONFBY_DIRECTIVE_H
#define NGX_CONFBY_DIRECTIVE_H

#include "ngx_confby_common.h"

char * ngx_confby_lua_package_cpath(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char * ngx_confby_lua_package_path(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char * ngx_confby_lua(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

#endif /* NGX_CONFBY_DIRECTIVE_H */

