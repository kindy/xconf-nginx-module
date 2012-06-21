/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef NGX_CONFBY_COMMON_H
#define NGX_CONFBY_COMMON_H

#include <nginx.h>
#include <ngx_core.h>

#include <setjmp.h>
#include <stdint.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

typedef struct {
    lua_State       *lua;

    ngx_str_t        lua_path;
    ngx_str_t        lua_cpath;

    ngx_pool_t      *pool;

} ngx_confby_conf_t;


extern ngx_module_t ngx_confby_module;


#endif /* NGX_CONFBY_COMMON_H */

