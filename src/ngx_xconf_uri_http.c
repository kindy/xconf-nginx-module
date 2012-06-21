/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

/*
 * lots of code from https://github.com/dermesser/echosrv
 * thanks dermesser
 */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_xconf_common.h"
#include "ngx_xconf_directive.h"


static int ngx_xconf_uri_http_get(ngx_str_t *host, ngx_str_t *port, ngx_str_t *url, ngx_conf_t *cf, ngx_xconf_ctx_t *ctx);


char *
ngx_xconf_include_uri_http(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx)
{
    ngx_str_t   in, hostname, port, url;
    size_t      max_port_len = 5;
    u_char      c;
    size_t      i, s;

    /* 初始化 {{{ */
    in.len = ctx->noscheme_uri.len - 2;
    in.data = ctx->noscheme_uri.data + 2;

    /* 长度 +1 是因为 getaddrinfo 函数需要 \0 结尾 */
    hostname.len = 0;
    hostname.data = ngx_palloc(cf->pool, in.len + 1);
    port.len = 0;
    port.data = ngx_palloc(cf->pool, max_port_len + 1);
    /* }}} */

    /* 解析 hostname, port, url {{{ */
    i = 0;
    s = 1; /* 1->host, 2->port, 3->found '/' */
    /* localhost:82/h/git */
    while (i < in.len) {
        c = in.data[i];
        i++;

        if (c == '/') {
            s = 3;
            break;
        }

        if (s == 1) {
            if (c == ':') {
                s = 2;
            } else {
                hostname.data[hostname.len++] = c;
            }
        } else if (s == 2) {
            if (c >= '0' && c <= '9') {
                if (port.len >= max_port_len) {
                    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                            "http: port is too long.");

                    return NGX_CONF_ERROR;
                }

                port.data[port.len++] = c;
            } else {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "http: port is not valid.");

                return NGX_CONF_ERROR;
            }
        }
    }

    if (hostname.len == 0 && port.len == 0) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http: hostname and port can not both empty (%V).",
                &in);

        return NGX_CONF_ERROR;
    } else if (hostname.len == 0) {
        hostname.len = sizeof("127.0.0.1") - 1;
        ngx_memcpy(hostname.data, "127.0.0.1", hostname.len);
    } else if (port.len == 0) {
        port.len = sizeof("80") - 1;
        ngx_memcpy(port.data, "80", port.len);
    }
    hostname.data[hostname.len + 1] = '\0';
    port.data[port.len + 1] = '\0';

    if (s == 3 && i <= in.len) {
        url.len = in.len - i + 1;
        url.data = in.data + i - 1;
    } else {
        url.len = 1;
        url.data = ngx_palloc(cf->pool, 1);
        url.data[0] = '/';
    }

    ngx_log_error(NGX_LOG_INFO, cf->log, 0,
            "\n- - - - http - - - -\nhost: %V\nport: %V\nurl: %V\n- - - - - - - -",
            &hostname, &port, &url);
    /* }}} */


    /* 获取 http 数据 {{{ */
    if (ngx_xconf_uri_http_get(&hostname, &port, &url, cf, ctx) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* }}} */

    return NGX_CONF_OK;
}


/*
 * 1. 连接获取数据错误
 * 2. 写文件错误 (x.tmp -> mv)
 */
static int
ngx_xconf_uri_http_get(ngx_str_t *host, ngx_str_t *port, ngx_str_t *url, ngx_conf_t *cf, ngx_xconf_ctx_t *ctx)
{
    struct addrinfo     hints;
    ngx_str_t           file, tmpfile;
    int                 connfd, rwlen, crwlen, cachefd, cwritedone;
    size_t              buflen = 1024 * 50;
    u_char              buf[1024 * 50], *p, *last;
    struct addrinfo     *srvinfo;

    /* 创建连接 {{{ */
    srvinfo = ngx_palloc(cf->pool, sizeof(struct addrinfo));

    connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connfd < 0) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http get: create socket error.");

        return NGX_ERROR;
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo((char *)(host->data), (char *)(port->data), &hints, &srvinfo)) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http get: getaddrinfo error.");

        return NGX_ERROR;
    }

    if (connect(connfd, srvinfo->ai_addr, srvinfo->ai_addrlen)) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http get: connect socket error.");

        return NGX_ERROR;
    }
    /* }}} */

    /* 发请求到服务器 {{{ */
    last = buf + buflen;
    p = buf;

    p = ngx_slprintf(p, last,
            "GET %V HTTP/1.0\r\nHost: %V\r\nUser-Agent: ngx-xconf\r\n\r\n",
            url, host);

    rwlen = p - buf;
    if (rwlen != ngx_write_fd(connfd, buf, rwlen)) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http get: write to server error.");

        goto error;
    }
    /* }}} */

    /* 准备配置文件句柄，把从服务器读到的 conf 写下来 {{{ */
    file.len = ctx->cachefile.len;
    file.data = ngx_palloc(cf->pool, file.len + 1);
    ngx_memcpy(file.data, ctx->cachefile.data, file.len);
    file.data[file.len] = '\0';

    tmpfile.len = file.len + 4;
    tmpfile.data = ngx_palloc(cf->pool, tmpfile.len + 1); /* for open '\0' */
    ngx_memcpy(tmpfile.data, ".tmp", 4);
    ngx_memcpy(tmpfile.data + 4, file.data, file.len);
    tmpfile.data[tmpfile.len] = '\0';

    cachefd = open((char *) tmpfile.data, O_WRONLY);
    /* }}} */

    cwritedone = 0;
    while ((rwlen = read(connfd, buf, buflen))) {
        if (rwlen == -1) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "http get: read from server error.");

            goto error;
        }

        crwlen = write(cachefd, buf, rwlen);
        if (crwlen != rwlen) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "http get: write data to tmpfile error.");

            goto error;
        }
        cwritedone = 1;

        ngx_log_error(NGX_LOG_INFO, cf->log, 0,
                "\n- - - - http get - - - -\n%*s\n- - - - - - - -",
                rwlen, buf);
    }

    if (cwritedone) {
        if (! rename((char *) tmpfile.data, (char *) file.data)) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "http get: move tmpfile to file error.");

            goto error;
        }
    } else {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http get: got non-data from server error.");

        goto error;
    }

    close(connfd);
    close(cachefd);

    return NGX_OK;

error:
    close(connfd);
    close(cachefd);

    return NGX_ERROR;
}

