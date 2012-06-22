/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_xconf_common.h"
#include "ngx_xconf_directive.h"

static size_t max_scheme_len = 100;


char *
ngx_xconf_include_uri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t          *arg, uri, filename;
    ngx_xconf_ctx_t     ctx;
    ngx_flag_t          is_last_elt;
    ngx_uint_t          i;
    ngx_str_t          *cmd_name;
    u_char              need_next;
    ngx_str_t           ofilename_prefix = ngx_string("$file"),
                        ofilename_suffix = ngx_string("l$line.conf"),
                        ofilename_suffix2 = ngx_string("conf");
    char               *rv;
    u_char             *s; /* 用于临时分配内存用 */;
    lua_State          *L;

    /* 参数说明见 README */

    uri.len = 0;        /* 如果没设定报错 */
    filename.len = 0;   /* 如果没设定就默认给一个 */
    ctx.evaluri = 1;    /* 默认 eval uri */
    ctx.pre_usecache = -1;   /* 默认不使用 cachefile */
    ctx.fail_usecache = -1;   /* 默认不使用 cachefile */

    arg = cf->args->elts;

    cmd_name = &arg[0];

    need_next = 0;
    is_last_elt = 0;

    /* 解析参数 {{{ */
    for (i = 1; i < cf->args->nelts; i++) {
        if (! (arg[i].len)) {
            continue;
        }

        is_last_elt = i == cf->args->nelts - 1;

        if (need_next) {
            switch(need_next) {
                case 'o':
                    filename.data = arg[i].data;
                    filename.len = arg[i].len;
                    break;
                case 'O':
                    filename.len = ofilename_prefix.len + arg[i].len + ofilename_suffix.len + sizeof("..") - 1;
                    filename.data = ngx_palloc(cf->pool, filename.len + 1);
                    ngx_snprintf(filename.data, filename.len,
                            "%V.%V.%V",
                            &ofilename_prefix, &arg[i], &ofilename_suffix);
                    break;
                case 'I':
                    filename.len = ofilename_prefix.len + arg[i].len + ofilename_suffix2.len + sizeof("..") - 1;
                    filename.data = ngx_palloc(cf->pool, filename.len + 1);
                    ngx_snprintf(filename.data, filename.len,
                            "%V.%V.%V",
                            &ofilename_prefix, &arg[i], &ofilename_suffix2);
                    break;
                case 't':
                    ctx.timeout = ngx_parse_time(&arg[i], 1);
                    if (ctx.timeout == NGX_ERROR) {
                        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                                "%V: option [-t %V] value error.",
                                cmd_name, &arg[i]);

                        return NGX_CONF_ERROR;
                    }
                    break;
                case 'c':
                    if (arg[i].len == 2 && arg[i].data[0] == '-' && arg[i].data[1] == '1') {
                        ctx.pre_usecache = -1 * (arg[i].data[1] - '0');
                    } else {
                        ctx.pre_usecache = ngx_parse_time(&arg[i], 1);
                        if (ctx.pre_usecache == NGX_ERROR) {
                            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                                    "%V: option [-c %V] value error.",
                                    cmd_name, &arg[i]);

                            return NGX_CONF_ERROR;
                        }
                    }
                    break;
                case 'C':
                    if (arg[i].len == 2 && arg[i].data[0] == '-' &&
                            (arg[i].data[1] >= '1' && arg[i].data[1] <= '2')) {
                        ctx.fail_usecache = -1 * (arg[i].data[1] - '0');
                    } else {
                        ctx.fail_usecache = ngx_parse_time(&arg[i], 1);
                        if (ctx.fail_usecache == NGX_ERROR) {
                            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                                    "%V: option [-C %V] value error.",
                                    cmd_name, &arg[i]);

                            return NGX_CONF_ERROR;
                        }
                    }
                    break;
                default:
                    /* 通常不会到这里 */
                    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                            "%V: unknown option [%c] .",
                            cmd_name, need_next);

                    return NGX_CONF_ERROR;
            }

            need_next = 0;
            continue;
        } else {
            if (arg[i].data[0] == '-') {
                /* 现在只支持 -x */
                if (arg[i].len != 2) {
                    goto unknow_opt;
                }
                switch(arg[i].data[1]) {
                    case 'n':
                        ctx.evaluri = 0;
                        break;
                    case 'c':
                    case 'C':
                    case 'o':
                    case 'I':
                    case 'O':
                    case 't':
                        need_next = arg[i].data[1];

                        if (is_last_elt) {
                            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                                    "%V: option [-%c] need a value.",
                                    cmd_name, need_next);

                            return NGX_CONF_ERROR;
                        }

                        break;
                    default:
                        goto unknow_opt;
                }

                continue;
            } else { /* this will be the uri */
                uri.data = arg[i].data;
                uri.len = arg[i].len;

                continue;
            }
        }

        goto unknow_opt;
    }
    /* }}} */

    /* 因为上面不需要用到 lua 的功能，所有此刻才初始化 lua vm {{{ */
    L = luaL_newstate();

    if (L == NULL) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "%V: create lua vm fail.",
                cmd_name);

        return NGX_CONF_ERROR;
    }

    /* XXX 从这以下如果出错，需要 goto error; 因为要 close lua vm */

    ctx.lua = L;

    luaL_openlibs(L);
    {
        int         rc;
        ngx_str_t   msg;

        rc = luaL_loadbuffer(L, (const char*) xconf_util_lua, sizeof(xconf_util_lua), "ngx_xconf_util.lua(embed)");
        if (rc != 0) {
            msg.data = (u_char *) lua_tolstring(L, -1, &msg.len);

            if (msg.data == NULL) {
                msg.data = (u_char *) "unknown reason";
                msg.len = sizeof("unknown reason") - 1;
            }

            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "xconf load lua code error: %V.",
                    &msg);

            lua_pop(L, 1);

            goto error;
        }
    }
    /* 这个 lua 文件会创建 若干个 全局函数，如: format */

    if (ngx_xconf_util_lua_pcall(cf, L, 0, 0, 0, 0) != NGX_OK) {
        goto error;
    }
    /* }}} */

    /* 创建变量 table 到 lua vm (var_ctx) 里，用于变量插值 {{{ */
    lua_createtable(L, /* i */0, /* key */10); /* var_ctx */
    lua_pushlstring(L, (char *)cf->conf_file->file.name.data, cf->conf_file->file.name.len);
    lua_setfield(L, -2, "file");
    lua_pushnumber(L, cf->conf_file->line);
    lua_setfield(L, -2, "line");
    lua_pushlstring(L, (char *)cf->cycle->prefix.data, cf->cycle->prefix.len);
    lua_setfield(L, -2, "prefix");
    lua_pushlstring(L, (char *)cf->cycle->conf_prefix.data, cf->cycle->conf_prefix.len);
    lua_setfield(L, -2, "conf_prefix");
    lua_pushnumber(L, (int) ngx_pid);
    lua_setfield(L, -2, "pid");
    lua_pushlstring(L, (char *)cf->cycle->hostname.data, cf->cycle->hostname.len);
    lua_setfield(L, -2, "hostname");
    {
        ngx_time_t *tp = ngx_timeofday();

        lua_pushnumber(L, (int) (tp->sec));
        lua_setfield(L, -2, "time"); /* var_ctx */
    }

    lua_setfield(L, LUA_GLOBALSINDEX, "var_ctx");
    /* }}} */

    if (ctx.evaluri) {
        lua_getfield(L, LUA_GLOBALSINDEX, "format"); /* got the format function */
        lua_pushlstring(L, (char *) uri.data, uri.len);
        if (ngx_xconf_util_lua_pcall(cf, L, 1, 1, 0, 0) != NGX_OK) {
            goto error;
        }
        uri.data = (u_char *) lua_tolstring(L, -1, &uri.len);
        s = ngx_palloc(cf->pool, uri.len);
        ngx_memcpy(s, uri.data, uri.len);
        uri.data = s;
        s = NULL;
        lua_pop(L, 1);
    }

    if (! (uri.len)) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "%V: must give us uri.",
                cmd_name);

        goto error;
    }

    ctx.uri.len = uri.len;
    ctx.uri.data = uri.data;

    /* 计算 uri 的 scheme {{{ */
    /* 如果以 '//' 开头 认为是 http: */
    if (uri.len > 2 && uri.data[0] == '/' && uri.data[1] == '/') {
        ctx.noscheme_uri.data = uri.data;
        ctx.noscheme_uri.len = uri.len;

        ctx.scheme.len = sizeof("http") - 1;
        ctx.scheme.data = ngx_palloc(cf->pool, ctx.scheme.len);
        ctx.scheme.data[0] = 'h';
        ctx.scheme.data[1] = 't';
        ctx.scheme.data[2] = 't';
        ctx.scheme.data[3] = 'p';
    /* 如果以 '/' 或 './' 开头 认为是 file: */
    } else if (uri.data[0] == '/'
            || (uri.len > 2 && uri.data[0] == '.' && uri.data[1] == '/')) {

        size_t is_abs = uri.data[0] == '/';
        u_char *data = uri.data + (is_abs ? 0 : 2);
        size_t len = uri.len - (is_abs ? 0 : 2);

        ctx.noscheme_uri.len = len + 2;
        ctx.noscheme_uri.data = ngx_palloc(cf->pool, len + 2);
        ctx.noscheme_uri.data[0] = '/';
        ctx.noscheme_uri.data[1] = '/';

        /* XXX len-- 的行为是否完全可预测 ???? */
        while (len-- > 0) { ctx.noscheme_uri.data[2 + len] = data[len]; }

        ctx.scheme.len = sizeof("file") - 1;
        ctx.scheme.data = ngx_palloc(cf->pool, ctx.scheme.len);
        ctx.scheme.data[0] = 'f';
        ctx.scheme.data[1] = 'i';
        ctx.scheme.data[2] = 'l';
        ctx.scheme.data[3] = 'e';
    } else {
        u_char      c;
        ngx_str_t   scheme;
        size_t      i, maxi, found;

        /* find scheme: [a-z_][a-z_0-9+.-]: , max_scheme_len */
        scheme.data = ngx_palloc(cf->pool, max_scheme_len);
        scheme.len = 0;
        i = 0;
        maxi = uri.len - 1;
        found = 0;

        while (i < max_scheme_len && i <= maxi) {
            c = uri.data[i];

            if (c == ':') {
                found = 1;
                break;
            }

            scheme.data[i] = c;
            scheme.len++;
            if ((c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || c == '+' || c == '-' || c == '.') {
            } else {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "%V: uri (%V...) have no valid scheme name.",
                        cmd_name, &scheme);

                goto error;
            }

            /* XXX important */
            i++;
        }

        if (! (found && scheme.len)) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: uri (%V) have no valid scheme name.",
                    cmd_name, &uri);

            goto error;
        }

        ctx.scheme.len = scheme.len;
        ctx.scheme.data = scheme.data;

        /* 1 -> the ':' after scheme name */
        ctx.noscheme_uri.len = uri.len - scheme.len - 1;
        ctx.noscheme_uri.data = uri.data + scheme.len + 1;
    }
    /* }}} */

    /* XXX 有些 scheme 不需要 filename ，如 file {{{ */
    if (! (filename.len)) {
        filename.len = ofilename_prefix.len + ofilename_suffix.len + sizeof(".") - 1;
        filename.data = ngx_palloc(cf->pool, filename.len + 1);
        ngx_snprintf(filename.data, filename.len,
                "%V.%V",
                &ofilename_prefix, &ofilename_suffix);
    }

    /* }}} */
    /* FIXME filename.data 是否需要 free ?? */
    lua_getfield(L, LUA_GLOBALSINDEX, "format"); /* got the format function */
    lua_pushlstring(L, (char *) filename.data, filename.len);
    if (ngx_xconf_util_lua_pcall(cf, L, 1, 1, 0, 0) != NGX_OK) {
        goto error;
    }
    filename.data = (u_char *) lua_tolstring(L, -1, &filename.len);
    s = ngx_palloc(cf->pool, filename.len + 1);
    ngx_cpystrn(s, filename.data, filename.len + 1); /* 让 ngx_cpystrn 帮忙添加个 \0 */
    filename.data = s;
    s = NULL;
    lua_pop(L, 1);

    if (ngx_conf_full_name(cf->cycle, &filename, 1) != NGX_OK) {
        goto error;
    }

    ctx.cachefile.len = filename.len;
    ctx.cachefile.data = filename.data;

    ngx_log_error(NGX_LOG_INFO, cf->log, 0,
            "\n- - - - - - - -\ncmd_name: %V\nfileneme: %V\nuri: %V\npre_usecache: %d\nfail_usecache: %d\nevaluri: %d\nscheme: %V\nnoscheme_uri: %V\n- - - - - - - -",
            cmd_name, &filename, &uri, ctx.pre_usecache, ctx.fail_usecache, ctx.evaluri, &ctx.scheme, &ctx.noscheme_uri);


    /* 进入 具体执行逻辑 {{{ */
    if (! ngx_strncmp(ctx.scheme.data, "file", ctx.scheme.len)) {
        if (ngx_xconf_include_uri_file(cf, cmd, conf, &ctx) == NGX_CONF_ERROR) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: run file error.",
                    cmd_name);

            return NGX_CONF_ERROR;
        }
    } else if (! ngx_strncmp(ctx.scheme.data, "http", ctx.scheme.len)) {
        if (ngx_xconf_include_uri_http(cf, cmd, conf, &ctx) == NGX_CONF_ERROR) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: run http error.",
                    cmd_name);

            return NGX_CONF_ERROR;
        } else {
            rv = ngx_conf_parse(cf, &ctx.cachefile);

            if (rv != NGX_CONF_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }
    /* }}} */

    goto done;

done:
    lua_close(L);

    return NGX_CONF_OK;

unknow_opt:
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
            "%V: unknown option [%V].",
            cmd_name, &arg[i]);

    return NGX_CONF_ERROR;

error:
    lua_close(L);

    return NGX_CONF_ERROR;
}


int
ngx_xconf_util_lua_pcall(ngx_conf_t *cf, lua_State *L, int nargs, int nresults, int errfunc, int keeperrmsg)
{
    int         rc;
    ngx_str_t   msg;

    rc = lua_pcall(L, nargs, nresults, errfunc);

    if (rc != 0) {
        msg.data = (u_char *) lua_tolstring(L, -1, &msg.len);

        if (! keeperrmsg) {
            lua_pop(L, 1);
        }

        if (msg.data == NULL) {
            msg.data = (u_char *) "unknown reason";
            msg.len = sizeof("unknown reason") - 1;
        }

        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "xconf run lua error: %V.",
                &msg);

        lua_pop(L, 1);
        return NGX_ERROR;
    }

    return NGX_OK;
}

