/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_confby_common.h"
#include "ngx_confby_directive.h"


char *
ngx_confby_lua_package_cpath(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_confby_conf_t        *cbcf = conf;
    ngx_str_t                *value;

    if (cbcf->lua_cpath.len != 0) {
        return "is duplicate";
    }

    dd("enter");

    value = cf->args->elts;

    cbcf->lua_cpath.len = value[1].len;
    cbcf->lua_cpath.data = value[1].data;

    return NGX_CONF_OK;
}


char *
ngx_confby_lua_package_path(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_confby_conf_t        *cbcf = conf;
    ngx_str_t                *value;

    if (cbcf->lua_path.len != 0) {
        return "is duplicate";
    }

    dd("enter");

    value = cf->args->elts;

    cbcf->lua_path.len = value[1].len;
    cbcf->lua_path.data = value[1].data;

    return NGX_CONF_OK;
}


char *
ngx_confby_lua(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_confby_conf_t        *cbcf;
    cbcf = (ngx_confby_conf_t *) conf;

    size_t      len;
    ngx_str_t   *value, cur_conf_file_name, gen_conf_file_name, lua_code, conf_con;
    FILE        *gen_conf_f;
    lua_State   *L;
    int         rc;
    u_char      *err_msg;

    L = cbcf->lua;

    if (L == NULL) {
        return NGX_CONF_ERROR;
    }

    /* */
    value = cf->args->elts;

    cur_conf_file_name = cf->conf_file->file.name;

    len = cur_conf_file_name.len;
    /* user specifiy conf file name */
    if (cf->args->nelts == 3 && value[2].len) {
        /* FIXME conf file name valid */
        len += value[2].len + 5;
    } else {
        len += 30;
    }

    gen_conf_file_name.data = ngx_palloc(cf->pool, len);

    if (gen_conf_file_name.data == NULL) {
        return NGX_CONF_ERROR;
    }

    if (cf->args->nelts == 3 && value[2].len) {
        ngx_snprintf(gen_conf_file_name.data, len, "%*s.%*s", cur_conf_file_name.len, cur_conf_file_name.data, value[2].len, value[2].data);
    } else {
        /* 使用当前 $file.l$line.conf 作为新文件名 */
        ngx_snprintf(gen_conf_file_name.data, len, "%*s.l%04d.conf", cur_conf_file_name.len, cur_conf_file_name.data, cf->conf_file->line);
    }

    gen_conf_file_name.len = strlen((char *)gen_conf_file_name.data);

    lua_code = value[1];

    rc = luaL_loadbuffer(L, (char *)lua_code.data, lua_code.len, "conf_by_lua");
    /* return function(...) {user_code} end */

    if (rc == LUA_ERRSYNTAX) {
        ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "lua syntax error at %*s:%d", cur_conf_file_name.len, cur_conf_file_name.data, cf->conf_file->line);
        return NGX_CONF_ERROR;
    } else if (rc) {
        // memory allocation error or ...
        return NGX_CONF_ERROR;
    }

    /* add opt {
     *  conf_file
     *  conf_prefix
     *  prefix
     * }
     */
    lua_createtable(L, 0 /* narr */, 3 /* nrec */);
    lua_pushlstring(L, (char *)cur_conf_file_name.data, cur_conf_file_name.len);
    lua_setfield(L, -2, "conf_file");
    lua_pushlstring(L, (char *)cf->cycle->conf_prefix.data, cf->cycle->conf_prefix.len);
    lua_setfield(L, -2, "conf_prefix");
    lua_pushlstring(L, (char *)cf->cycle->prefix.data, cf->cycle->prefix.len);
    lua_setfield(L, -2, "prefix");

    rc = lua_pcall(L, 1, 1, 0);

    if (rc != 0) {
        /*  error occured when running loaded code */
        err_msg = (u_char *) lua_tolstring(L, -1, &len);

        if (err_msg == NULL) {
            err_msg = (u_char *) "unknown reason";
            len = sizeof("unknown reason") - 1;
        }

        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "run lua code at %*s:%d fail: [%*s]", cur_conf_file_name.len, cur_conf_file_name.data, cf->conf_file->line,
                len, err_msg);

        return NGX_CONF_ERROR;
    }

    conf_con.data = (u_char *) lua_tolstring(L, -1, &(conf_con.len));

    /* 如果 lua 没有返回结果 */
    if (! conf_con.len) {
        return NGX_CONF_OK;
    }

    gen_conf_f = fopen((char *)gen_conf_file_name.data, "w");
    fprintf(gen_conf_f, "# generate with conf_by_lua at %*s:%d\n\n", (int) cur_conf_file_name.len, (char *) cur_conf_file_name.data, (int) cf->conf_file->line);

    if (fprintf(gen_conf_f, "%*s", (int) conf_con.len, (char *) conf_con.data) != (int) conf_con.len) {
        return NGX_CONF_ERROR;
    }
    fclose(gen_conf_f);

    return ngx_conf_parse(cf, &gen_conf_file_name);

    // ngx_pfree(cf->pool, gen_conf_file_name.data);
    // return NGX_CONF_OK;
}

