/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_confby_directive.h"

#if !defined(nginx_version) || nginx_version < 8054
#error "at least nginx 0.8.54 is required"
#endif

static void * ngx_confby_create_conf(ngx_cycle_t *cycle);
static char * ngx_confby_done(ngx_cycle_t *cycle, void *conf);
static lua_State * ngx_confby_lua_new_state(ngx_cycle_t *cycle, ngx_confby_conf_t *cbcf);

static ngx_command_t ngx_confby_commands[] = {

    { ngx_string("confby_lua_package_cpath"),
      NGX_DIRECT_CONF|NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_confby_lua_package_cpath,
      0,
      0,
      NULL },

    { ngx_string("confby_lua_package_path"),
      NGX_DIRECT_CONF|NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_confby_lua_package_path,
      0,
      0,
      NULL },

    { ngx_string("confby_lua"),
      NGX_DIRECT_CONF|NGX_ANY_CONF|NGX_CONF_TAKE12,
      ngx_confby_lua,
      0,
      0,
      NULL },

    ngx_null_command
};


static ngx_core_module_t  ngx_confby_module_ctx = {
    ngx_string("confby"),           /* name */
    ngx_confby_create_conf,         /* create_conf */
    ngx_confby_done                 /* init_conf - call after conf parse done */
};


ngx_module_t  ngx_confby_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_confby_commands,                   /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_confby_create_conf(ngx_cycle_t *cycle)
{
    ngx_confby_conf_t *cbcf;

    cbcf = ngx_pcalloc(cycle->pool, sizeof(ngx_confby_conf_t));
    if (cbcf == NULL) {
        return NULL;
    }

    /* create new Lua VM instance */
    cbcf->lua = ngx_confby_lua_new_state(cycle, cbcf);
    if (cbcf->lua == NULL) {
        return NULL;
    }

    cbcf->lua_path.data = (u_char *) "abc";
    cbcf->lua_path.len = (size_t) 3;

    return cbcf;
}


static char *
ngx_confby_done(ngx_cycle_t *cycle, void *conf)
{
    ngx_confby_conf_t *cbcf = conf;

    /* clean up cbcf->lua */
    if (cbcf->lua != NULL) {
        lua_close(cbcf->lua);
    }

    return NGX_CONF_OK;
}


static lua_State *
ngx_confby_lua_new_state(ngx_cycle_t *cycle, ngx_confby_conf_t *cbcf)
{
    lua_State       *L;
    // const char      *old_path;
    // const char      *new_path;
    // size_t           old_path_len;
    // const char      *old_cpath;
    // const char      *new_cpath;
    // size_t           old_cpath_len;

    // ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "lua creating new conf vm state");

    L = luaL_newstate();
    if (L == NULL) {
        return NULL;
    }

    luaL_openlibs(L);

    lua_getglobal(L, "package");

    if (!lua_istable(L, -1)) {
        // ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
        //         "the \"package\" table does not exist");
        return NULL;
    }

#ifdef LUA_DEFAULT_PATH
#   define LUA_DEFAULT_PATH_LEN (sizeof(LUA_DEFAULT_PATH) - 1)
    // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //         "lua prepending default package.path with %s", LUA_DEFAULT_PATH);

    lua_pushliteral(L, LUA_DEFAULT_PATH ";"); /* package default */
    lua_getfield(L, -2, "path"); /* package default old */
    lua_concat(L, 2); /* package new */
    lua_setfield(L, -2, "path"); /* package */
#endif

#ifdef LUA_DEFAULT_CPATH
#   define LUA_DEFAULT_CPATH_LEN (sizeof(LUA_DEFAULT_CPATH) - 1)
    // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //         "lua prepending default package.cpath with %s", LUA_DEFAULT_CPATH);

    lua_pushliteral(L, LUA_DEFAULT_CPATH ";"); /* package default */
    lua_getfield(L, -2, "cpath"); /* package default old */
    lua_concat(L, 2); /* package new */
    lua_setfield(L, -2, "cpath"); /* package */
#endif

    /* 添加 nginx_prefix/?.lua 到 package.path 中 */
    if (cycle->prefix.len != 0) {
        lua_pushlstring(L, (char *)cycle->prefix.data, cycle->prefix.len);
        lua_pushstring(L, "/?.lua;");
        lua_pushlstring(L, (char *)cycle->prefix.data, cycle->prefix.len);
        lua_pushstring(L, "/?/init.lua;");
        lua_getfield(L, -5, "path"); /* get original package.path */
        lua_concat(L, 5); /* new path */
        lua_setfield(L, -2, "path"); /* package */
    }

    lua_remove(L, -1); /* remove the "package" table */

    return L;
}

