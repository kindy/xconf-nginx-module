/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_xconf_common.h"
#include "ngx_xconf_directive.h"

static size_t max_scheme_len = 100;

static ngx_xconf_scheme_t ngx_xconf_schemes[] = {
    { ngx_string("file"),
      ngx_xconf_include_uri_file,
      1,
      0, 0, 0
    },

    { ngx_string("http"),
      ngx_xconf_include_uri_http,
      1,
      1, 1, 1
    },

    /*
    { ngx_string("lua"),
      ngx_xconf_include_uri_lua,
      1,
      1, 1, 1
    },

    { ngx_string("luai"),
      ngx_xconf_include_uri_luai,
      1,
      1, 1, 1
    },
    */

    { ngx_null_string, NULL, 0, 0, 0, 0 }
};


char *
ngx_xconf_include_uri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t          *arg, uri, filename, scheme_name;
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
    ngx_xconf_scheme_t *scheme;


    arg = cf->args->elts;
    cmd_name = &arg[0];

    scheme = ngx_xconf_schemes;
    if ((scheme == NULL) || (scheme->name.len == 0)) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "%V: no scheme define.",
                cmd_name);

        return NGX_CONF_ERROR;
    }

    /* 参数说明见 README */

    uri.len = 0;        /* 如果没设定报错 */
    filename.len = 0;   /* 如果没设定就默认给一个 */
    ctx.evaluri = 1;    /* 默认 eval uri */
    ctx.keep_error_cachefile = 0;   /* 默认删除解析出错的 cachefile */
    ctx.pre_usecache = -1;   /* 默认不使用 cachefile */
    ctx.fail_usecache = -1;   /* 默认不使用 cachefile */

    ctx.fetch_content = NULL;
    ctx.do_cachefile = 0;

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
                    case 'K':
                        ctx.keep_error_cachefile = 1;
                        break;
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

            rv = NULL;
            goto error;
        }
    }
    /* 这个 lua 文件会创建 若干个 全局函数，如: format */

    if (ngx_xconf_util_lua_pcall(cf, L, 0, 0, 0, 0) != NGX_OK) {
        rv = NULL;
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

    /* 对 uri 进行变量插值 {{{ */
    if (ctx.evaluri) {
        lua_getfield(L, LUA_GLOBALSINDEX, "format"); /* got the format function */
        lua_pushlstring(L, (char *) uri.data, uri.len);
        if (ngx_xconf_util_lua_pcall(cf, L, 1, 1, 0, 0) != NGX_OK) {
            rv = NULL;
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

        rv = NULL;
        goto error;
    }

    ctx.uri.len = uri.len;
    ctx.uri.data = uri.data;
    /* }}} */

    /* 计算 uri 的 scheme_name {{{ */
    /* 如果以 '//' 开头 认为是 http: */
    if (uri.len > 2 && uri.data[0] == '/' && uri.data[1] == '/') {
        ctx.noscheme_uri.data = uri.data;
        ctx.noscheme_uri.len = uri.len;

        scheme_name.len = sizeof("http") - 1;
        scheme_name.data = ngx_palloc(cf->pool, scheme_name.len + 1);
        ngx_cpystrn(scheme_name.data, (u_char *)"http", scheme_name.len + 1);
    /* 如果以 '/' 或 './' 开头 认为是 file: */
    } else if (uri.data[0] == '/'
            || (uri.len > 2 && uri.data[0] == '.' && uri.data[1] == '/')) {

        size_t is_abs = uri.data[0] == '/';
        u_char *data = uri.data + (is_abs ? 0 : 2);
        size_t len = uri.len - (is_abs ? 0 : 2);

        ctx.noscheme_uri.len = len + 2;
        ctx.noscheme_uri.data = ngx_palloc(cf->pool, len + 2 + 1);
        ctx.noscheme_uri.data[0] = '/';
        ctx.noscheme_uri.data[1] = '/';
        ngx_cpystrn(ctx.noscheme_uri.data + 2, data, len + 1);

        scheme_name.len = sizeof("file") - 1;
        scheme_name.data = ngx_palloc(cf->pool, scheme_name.len + 1);
        ngx_cpystrn(scheme_name.data, (u_char *)"file", scheme_name.len + 1);
    } else {
        u_char      c;
        ngx_str_t   tmp_scheme;
        size_t      i, maxi, found;

        /* find scheme: [a-z_][a-z_0-9+.-]: , max_scheme_len */
        tmp_scheme.data = ngx_palloc(cf->pool, max_scheme_len);
        tmp_scheme.len = 0;
        i = 0;
        maxi = uri.len - 1;
        found = 0;

        while (i < max_scheme_len && i <= maxi) {
            c = uri.data[i];

            if (c == ':') {
                found = 1;
                break;
            }

            tmp_scheme.data[i] = c;
            tmp_scheme.len++;
            if ((c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || c == '+' || c == '-' || c == '.') {
            } else {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "%V: uri (%V...) have no valid scheme name.",
                        cmd_name, &tmp_scheme);

                rv = NULL;
                goto error;
            }

            /* XXX important */
            i++;
        }

        if (! (found && tmp_scheme.len)) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: uri (%V) have no valid scheme name.",
                    cmd_name, &uri);

            rv = NULL;
            goto error;
        }

        scheme_name.len = tmp_scheme.len;
        scheme_name.data = tmp_scheme.data;
        scheme_name.data[tmp_scheme.len] = '\0';

        /* 1 -> the ':' after scheme name */
        ctx.noscheme_uri.len = uri.len - tmp_scheme.len - 1;
        ctx.noscheme_uri.data = uri.data + tmp_scheme.len + 1;
    }
    /* }}} */

    /* 查找当前 scheme {{{ */
    for ( /* void */ ; scheme->name.len; scheme++) {
        if (scheme_name.len == scheme->name.len &&
                ngx_strcmp(scheme_name.data, scheme->name.data) == 0) {
            break;
        }
    }

    if (! scheme->name.len) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "%V: no scheme found for \"%V\".",
                cmd_name, &ctx.scheme);

        rv = NULL;
        goto error;
    }
    /* }}} */

    ctx.scheme = scheme;

    /* 如果 scheme usecache 则计算 filename 并 判断 pre_usecache {{{ */
    if (scheme->usecache) {
        /* filename 计算, 插值, 展开 {{{ */
        if (! (filename.len)) {
            filename.len = ofilename_prefix.len + ofilename_suffix.len + sizeof(".") - 1;
            filename.data = ngx_palloc(cf->pool, filename.len + 1);
            ngx_snprintf(filename.data, filename.len,
                    "%V.%V",
                    &ofilename_prefix, &ofilename_suffix);
        }

        /* FIXME filename.data 是否需要 free ?? */
        lua_getfield(L, LUA_GLOBALSINDEX, "format"); /* got the format function */
        lua_pushlstring(L, (char *) filename.data, filename.len);
        if (ngx_xconf_util_lua_pcall(cf, L, 1, 1, 0, 0) != NGX_OK) {
            rv = NULL;
            goto error;
        }
        filename.data = (u_char *) lua_tolstring(L, -1, &filename.len);
        s = ngx_palloc(cf->pool, filename.len + 1);
        ngx_cpystrn(s, filename.data, filename.len + 1); /* 让 ngx_cpystrn 帮忙添加个 \0 */
        filename.data = s;
        s = NULL;
        lua_pop(L, 1);

        if (ngx_conf_full_name(cf->cycle, &filename, 1) != NGX_OK) {
            rv = NULL;
            goto error;
        }
        /* }}} */

        ctx.cachefile = ngx_palloc(cf->pool, sizeof(ngx_file_t));
        if (ctx.cachefile == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: alloc cachefile error.",
                    cmd_name);

            rv = NULL;
            goto error;
        }
        ngx_memzero(ctx.cachefile, sizeof(ngx_file_t));

        ctx.cachefile->name = filename;
        ctx.cachefile->log = cf->log;

        dd("pre-usecache: %d, %d", (int)ctx.scheme->pre_usecache, (int)ctx.pre_usecache);
        if (ctx.scheme->pre_usecache && ctx.pre_usecache > -1) {
            ngx_file_t  *file;

            file = ctx.cachefile;

            /* 获取文件信息 FAIL */
            if (ngx_file_info(file->name.data, &file->info) == -1) {
                /* 如果文件不存在则忽略，否则报错 */
                if (errno != ENOENT) {
                    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                            "%V: get file \"%V\" info error: %s.",
                            cmd_name, file->name, strerror(errno));

                    rv = NULL;
                    goto error;
                }
            /* 获取文件信息 OK, 有没可能是个 目录 ?? ，如果谁这么配置 !!! */
            } else {
                int     now;

                /* 只要文件存在就用 */
                if (ctx.pre_usecache == 0) {
                    goto do_cachefile;
                }

                ngx_time_update();
                now = (int)ngx_time();

                dd("pre-usecache now: %d, mtime: %d, diff: %d, pref: %d.",
                        now, (int)file->info.st_mtime, now - (int)file->info.st_mtime, (int)ctx.pre_usecache);

                if (now - file->info.st_mtime <= ctx.pre_usecache) {
                    goto do_cachefile;
                }
            }
        }
    }
    /* }}} */

    ngx_log_error(NGX_LOG_INFO, cf->log, 0,
            "\n- - - - - - - -\ncmd_name: %V\nfileneme: %V\nuri: %V\npre_usecache: %d\nfail_usecache: %d\nevaluri: %d\nscheme: %V\nnoscheme_uri: %V\n- - - - - - - -",
            cmd_name, &filename, &uri, ctx.pre_usecache, ctx.fail_usecache, ctx.evaluri, &ctx.scheme->name, &ctx.noscheme_uri);

    /* 执行具体 scheme {{{ */
    rv = scheme->handler(cf, cmd, conf, &ctx);

    if (rv == NGX_CONF_OK) {
        goto done;
    } else if (rv == NGX_XCONF_FETCH_ERROR) {
        /* do fail_usecache */
        if (scheme->usecache) {
            goto fetch_fail;
        } else {
            /* 不可能 */
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "if you see this error, please report it to author with \"%V\".",
                    &scheme->name);

            rv = NULL;
            goto error;
        }
    } else if (rv == NGX_XCONF_FETCH_OK) {
        if (scheme->usecache) {
            if (ctx.fetch_content != NULL) {
                goto save_cachefile;
            } else if (ctx.do_cachefile) {
                goto do_cachefile;
            }
        }

        goto done;
    } else {
        goto error;
    }
    /* }}} */

    goto done;

fetch_fail:
    dd("fail-usecache: %d, %d", (int)ctx.scheme->fail_usecache, (int)ctx.fail_usecache);

    if (ctx.scheme->fail_usecache && ctx.fail_usecache != -1) {
        /* 删除 */
        if (ctx.fail_usecache != -2) {
            ngx_delete_file(ctx.cachefile->name.data);

            rv = NULL;
            goto error;

        } else if (ctx.fail_usecache >= 0) {
            ngx_file_t  *file;

            file = ctx.cachefile;

            /* 获取文件信息 FAIL */
            if (ngx_file_info(file->name.data, &file->info) == -1) {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "%V: get file \"%V\" info in fetch_fail error: %s.",
                        cmd_name, file->name, strerror(errno));

                rv = NULL;
                goto error;
            /* 获取文件信息 OK, 有没可能是个 目录 ?? ，如果谁这么配置 !!! */
            } else {
                int     now;

                /* 只要文件存在就用 */
                if (ctx.fail_usecache == 0) {
                    goto do_cachefile;
                }

                ngx_time_update();
                now = (int)ngx_time();

                dd("fail-usecache now: %d, mtime: %d, diff: %d, pref: %d.",
                        now, (int)file->info.st_mtime, now - (int)file->info.st_mtime, (int)ctx.fail_usecache);

                if (now - file->info.st_mtime <= ctx.fail_usecache) {
                    goto do_cachefile;
                }
            }
        } else {
            /* 不可能 */

            rv = NULL;
            goto error;
        }
    }

    rv = NULL;
    goto error;

save_cachefile:
    {
        int rc;

        ctx.cachefile->fd = ngx_open_file(ctx.cachefile->name.data, O_WRONLY, O_CREAT | O_TRUNC , S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

        if (ctx.cachefile->fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: open (%V) error: \"%s\".",
                    cmd_name, &ctx.cachefile->name, strerror(errno));

            rv = NULL;
            goto error;
        }

        dd("open cachefd: %s, %d", (char *)ctx.cachefile->name.data, ctx.cachefile->fd);

        rc = ngx_write_fd(ctx.cachefile->fd, ctx.fetch_content->data, ctx.fetch_content->len);
        if (rc == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: write data to (%V) error: \"%s\".",
                    cmd_name, ctx.cachefile->name, strerror(errno));

            rv = NULL;
            goto error;
        } else if ((size_t)rc != ctx.fetch_content->len) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: write data to (%V) incomplete: %z of %uz.",
                    cmd_name, ctx.cachefile->name, rc, ctx.fetch_content->len);

            rv = NULL;
            goto error;
        }

        if (ngx_close_file(ctx.cachefile->fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%: close cachefile error.",
                    cmd_name);

            rv = NULL;
            goto error;
        }

        goto do_cachefile;
    }

do_cachefile:
    if (! scheme->usecache) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "%V: current scheme \"%V\" can not usecache.",
                cmd_name, &scheme->name);

        rv = NULL;
        goto error;
    }

    dd("do cachefile");
    rv = ngx_conf_parse(cf, &ctx.cachefile->name);

    if (rv != NGX_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "%V: do cachefile \"%V\" error.",
                cmd_name, &ctx.cachefile->name);

        /* 删除解析出错的 cachefile */
        if (! ctx.keep_error_cachefile) {
            if (ngx_delete_file(ctx.cachefile->name.data) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                            "%V: delete cachefile \"%V\" error.",
                            cmd_name, &ctx.cachefile->name);
            }
        }

        goto error;
    }

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

    return rv == NULL ? NGX_CONF_ERROR : rv;
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

