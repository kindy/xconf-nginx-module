/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_xconf_common.h"
#include "ngx_xconf_directive.h"


/*
include_uri uri;
    uri - 远程配置地址
        http://abc.com:81/xxx
        file:///home/abc/xx.conf or /home/abc/xx.conf
        x://lua/path/to/lua/file - 使用 lua 来执行后面路径指定的文件
            x://perl/path/to/perl
            x://python/path/to/python
            x://php/path/to/php
        - 可用变量
            $$ -> $
            $hostname -> 当前机器hostname
            $pid -> 当前 nginx master pid
    -o $file.abc.conf
        本地文件名，默认 "$file.l$line.conf"
        可用变量:
            $file - 当前配置文件
            $line - 当前行(指令结束行，受 nginx 所限)
            $conf_prefix - 配置文件 prefix
            $prefix - nginx 运行 prefix(nginx 启动时候 -p 设定目录)
    -O <xx>
        -o 的扩展，会把值放到 "$file.<xx>.l$line.conf"
    -t (timeout) 4m - uri 执行超时(仅 http 有效)
    -c (usecache) - 当 uri 处理失败时，是否使用本地 cachefile(-o 参数指定)
    -T (cachetime) 10m - cache 有效时长(仅在usecache时候有效)
        0 - (default) 无限长，只要有cachefile 就是用
        n - cachefile 的 mtime 是否大于 n，是就 fail

include_uri -O main -t 3s -c -T 3d http://config-server/web1.conf;
*/

static size_t max_scheme_len = 100;

char *
ngx_xconf_include_uri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t          *arg, uri, filename;
    ngx_xconf_ctx_t     ctx;
    ngx_flag_t          is_last_elt;
    ngx_uint_t          i;
    ngx_str_t           *cmd_name;
    u_char              need_next;
    ngx_str_t           ofilename_prefix = ngx_string("$file"),
                        ofilename_suffix = ngx_string("l$line.conf");

    /*
    -n
    -o $file.abc.conf
    -O <xx>
    -t (timeout) 4m - uri 执行超时(仅 http 有效)
    -c (usecache) - 当 uri 处理失败时，是否使用本地 cachefile(-o 参数指定)
    -T (cachetime) 10m - cache 有效时长(仅在usecache时候有效)
    */

    uri.len = 0;        /* 如果没设定报错 */
    filename.len = 0;   /* 如果没设定就默认给一个 */
    ctx.evaluri = 1;    /* 默认 eval uri */
    ctx.usecache = 0;   /* 默认不使用 cachefile */

    arg = cf->args->elts;

    cmd_name = &arg[0];

    need_next = 0;
    is_last_elt = 0;

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
                case 't':
                    ctx.timeout = ngx_parse_time(&arg[i], 1);
                    break;
                case 'T':
                    ctx.cachetime = ngx_parse_time(&arg[i], 1);
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
                        ctx.usecache = 1;
                        break;
                    case 'o':
                    case 'O':
                    case 't':
                    case 'T':
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

    if (! (uri.len)) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "%V: must give us uri.",
                cmd_name);

        return NGX_CONF_ERROR;
    }

    if (! (filename.len)) {
        filename.len = ofilename_prefix.len + ofilename_suffix.len + sizeof(".") - 1;
        filename.data = ngx_palloc(cf->pool, filename.len + 1);
        ngx_snprintf(filename.data, filename.len,
                "%V.%V",
                &ofilename_prefix, &arg[i], &ofilename_suffix);
    }


    ctx.uri.len = uri.len;
    ctx.uri.data = uri.data;

    /* 计算 uri 的 scheme */
    /* / || ./ */
    if (uri.data[0] == '/'
            || (uri.len > 2 && uri.data[0] == '.' && uri.data[1] == '/')) {
        ctx.scheme.len = sizeof("file") - 1;
        ctx.scheme.data = ngx_palloc(cf->pool, ctx.scheme.len);
        ctx.scheme.data[0] = 'f';
        ctx.scheme.data[1] = 'i';
        ctx.scheme.data[2] = 'l';
        ctx.scheme.data[3] = 'e';

        ctx.noscheme_uri.len = uri.len + (uri.data[0] == '/' ? 0 : -2);
        ctx.noscheme_uri.data = uri.data + (uri.data[0] == '/' ? 0 : 2);
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

                return NGX_CONF_ERROR;
            }

            /* XXX important */
            i++;
        }

        if (! (found && scheme.len)) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: uri (%V) have no valid scheme name.",
                    cmd_name, &uri);

            return NGX_CONF_ERROR;
        }

        ctx.scheme.len = scheme.len;
        ctx.scheme.data = scheme.data;

        /* 1 -> the ':' after scheme name */
        ctx.noscheme_uri.len = uri.len - scheme.len - 1;
        ctx.noscheme_uri.data = uri.data + scheme.len + 1;
    }

    ngx_log_error(NGX_LOG_WARN, cf->log, 0,
            "\n- - - - - - - -\ncmd_name: %V\nfileneme: %V\nuri: %V\nusecache: %d\nevalurl: %d\nscheme: %V\nnoscheme_uri: %V\n- - - - - - - -",
            cmd_name, &filename, &uri, ctx.usecache, ctx.evaluri, &ctx.scheme, &ctx.noscheme_uri);

    return NGX_CONF_OK;

unknow_opt:

    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
            "%V: unknown option [%V] .",
            cmd_name, arg[i]);

    return NGX_CONF_ERROR;
}
